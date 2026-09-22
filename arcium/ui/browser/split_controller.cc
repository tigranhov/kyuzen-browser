// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/split_controller.h"

#include <vector>

#include "arcium/browser/loose_page.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"

namespace arcium {

SplitController::SplitController(TabStripModel* tab_strip_model,
                                 SpaceSwitcher* switcher,
                                 SidebarTabModel* model)
    : tab_strip_model_(tab_strip_model), switcher_(switcher), model_(model) {}

SplitController::~SplitController() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

bool SplitController::CanSplit(int index_a, int index_b) const {
  if (!tab_strip_model_ || index_a == index_b) {
    return false;
  }
  if (!tab_strip_model_->ContainsIndex(index_a) ||
      !tab_strip_model_->ContainsIndex(index_b)) {
    return false;
  }
  // A pane is replaced by ending the split first. Chromium's own keyboard
  // path refuses the same case rather than swapping a tab out underneath a
  // divider.
  if (tab_strip_model_->GetSplitForTab(index_a) ||
      tab_strip_model_->GetSplitForTab(index_b)) {
    return false;
  }
  // A peek and the page in an outside link's small window are in no space, so
  // the space test below would refuse them anyway. Said outright, because a
  // refusal that rests on another rule's side effect is one a later change
  // can remove without noticing.
  if (IsLoosePage(tab_strip_model_->GetWebContentsAt(index_a)) ||
      IsLoosePage(tab_strip_model_->GetWebContentsAt(index_b))) {
    return false;
  }
  if (!switcher_) {
    return true;  // No spaces to disagree about.
  }
  // Two spaces may sit on two profiles, which would put two sets of logins on
  // one screen and hand the partition guard a tab visible in a space it does
  // not belong to. Even on one profile the sidebar has nowhere to draw the
  // half that belongs to the space the window is not showing.
  return switcher_->SpaceOfTabAt(index_a) == switcher_->SpaceOfTabAt(index_b);
}

bool SplitController::SplitWithActive(int index, std::optional<bool> on_right) {
  const int active = tab_strip_model_ ? tab_strip_model_->active_index() : -1;
  if (active < 0 || !CanSplit(active, index)) {
    return false;
  }
  // Taken before the split, because forming one reorders the strip to put the
  // two tabs together and the index this was called with is stale afterwards.
  const tabs::TabHandle joined =
      tab_strip_model_->GetTabAtIndex(index)->GetHandle();
  // One index, not two: AddToNewSplit splits what it is given with whatever
  // is active, and CHECKs that it was handed exactly one index that is not
  // the active one. CanSplit has already established both.
  const split_tabs::SplitTabId id = tab_strip_model_->AddToNewSplit(
      {index}, split_tabs::SplitTabVisualData(),
      split_tabs::SplitTabCreatedSource::kDragAndDropTab);
  if (on_right.has_value()) {
    PutOnSide(id, joined, *on_right);
  }
  return true;
}

bool SplitController::SplitWithActive(EntryId id,
                                      std::optional<bool> on_right) {
  if (!model_ || !tab_strip_model_) {
    return false;
  }
  const int active = tab_strip_model_->active_index();
  if (active < 0) {
    return false;
  }
  // The page on screen now, remembered before anything opens: activating the
  // entry puts its own tab in front, and splitting that with itself is not a
  // split.
  const tabs::TabHandle kept =
      tab_strip_model_->GetTabAtIndex(active)->GetHandle();
  // Opens a cold entry and binds the tab; a no-op for one that is already
  // warm, beyond putting it on screen.
  model_->ActivateEntry(id);
  const int opened =
      tab_strip_model_->GetIndexOfTab(tab_strip_model_->GetActiveTab());
  const int previous = tab_strip_model_->GetIndexOfTab(kept.Get());
  if (opened < 0 || previous < 0 || opened == previous) {
    // Nothing opened here: either the entry has no URL, or its tab belongs to
    // another window, which was raised instead. Neither is a split.
    return false;
  }
  // The entry's tab is the one that joins, so the page that was on screen
  // goes back in front first. Activating moves nothing, so `opened` still
  // names the entry's tab.
  tab_strip_model_->ActivateTabAt(previous);
  return SplitWithActive(opened, on_right);
}

void SplitController::PutOnSide(const split_tabs::SplitTabId& id,
                                tabs::TabHandle tab,
                                bool right) {
  const split_tabs::SplitTabData* const data =
      tab_strip_model_->GetSplitData(id);
  if (!data || !tab.Get()) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfTab(tab.Get());
  if (index < 0) {
    return;
  }
  // The lower of the two strip indices is the pane on the left.
  const bool is_on_right =
      index > static_cast<int>(data->GetIndexRange().start());
  if (is_on_right != right) {
    tab_strip_model_->ReverseTabsInSplit(id);
  }
}

void SplitController::Unsplit() {
  if (const std::optional<split_tabs::SplitTabId> id = SplitOfActive()) {
    tab_strip_model_->RemoveSplit(*id);
  }
}

void SplitController::ToggleSplitWithPrevious() {
  if (ActiveIsSplit()) {
    Unsplit();
    return;
  }
  if (!tab_strip_model_ || !previously_active_.Get()) {
    return;
  }
  const int previous =
      tab_strip_model_->GetIndexOfTab(previously_active_.Get());
  if (previous >= 0) {
    SplitWithActive(previous);
  }
}

void SplitController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Which tab the keyboard would split with. Taken from the strip's own
  // record of the activation rather than kept as a second history, so a tab
  // closed or moved away is simply a handle that no longer resolves.
  if (selection.active_tab_changed() && selection.old_tab &&
      selection.old_tab != selection.new_tab) {
    previously_active_ = selection.old_tab->GetHandle();
  }
}

void SplitController::EndSplitFor(int index) {
  if (!tab_strip_model_ || !tab_strip_model_->ContainsIndex(index)) {
    return;
  }
  if (const std::optional<split_tabs::SplitTabId> id =
          tab_strip_model_->GetSplitForTab(index)) {
    tab_strip_model_->RemoveSplit(*id);
  }
}

bool SplitController::ActiveIsSplit() const {
  return SplitOfActive().has_value();
}

std::optional<split_tabs::SplitTabId> SplitController::SplitOfActive() const {
  const int active = tab_strip_model_ ? tab_strip_model_->active_index() : -1;
  return active < 0 ? std::nullopt : tab_strip_model_->GetSplitForTab(active);
}

}  // namespace arcium
