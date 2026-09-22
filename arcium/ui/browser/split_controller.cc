// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/split_controller.h"

#include <vector>

#include "arcium/browser/loose_page.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"

namespace arcium {

SplitController::SplitController(TabStripModel* tab_strip_model,
                                 SpaceSwitcher* switcher)
    : tab_strip_model_(tab_strip_model), switcher_(switcher) {}

SplitController::~SplitController() = default;

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

bool SplitController::SplitWithActive(int index) {
  const int active = tab_strip_model_ ? tab_strip_model_->active_index() : -1;
  if (active < 0 || !CanSplit(active, index)) {
    return false;
  }
  // One index, not two: AddToNewSplit splits what it is given with whatever
  // is active, and CHECKs that it was handed exactly one index that is not
  // the active one. CanSplit has already established both.
  tab_strip_model_->AddToNewSplit(
      {index}, split_tabs::SplitTabVisualData(),
      split_tabs::SplitTabCreatedSource::kDragAndDropTab);
  return true;
}

void SplitController::Unsplit() {
  if (const std::optional<split_tabs::SplitTabId> id = SplitOfActive()) {
    tab_strip_model_->RemoveSplit(*id);
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
