// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/space_switcher.h"

#include <map>

#include "arcium/browser/entry_claim.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace arcium {

namespace {

// Keyed by TabStripModel* rather than held on BrowserView: the two patches
// this stage adds are handed a Browser or a TabStripModel and nothing
// richer, and a per-strip registry is reachable from both without dragging
// BrowserView into a unit test.
std::map<const TabStripModel*, SpaceSwitcher*>& Registry() {
  static base::NoDestructor<std::map<const TabStripModel*, SpaceSwitcher*>>
      registry;
  return *registry;
}

}  // namespace

SpaceSwitcher::SpaceSwitcher(TabStripModel* tab_strip_model,
                             ArciumModel* model,
                             TabBinding* binding)
    : tab_strip_model_(tab_strip_model),
      model_(model),
      binding_(binding),
      active_space_(model->last_active_space()) {
  Registry().emplace(tab_strip_model_.get(), this);
  tab_strip_model_->AddObserver(this);
  model_->AddObserver(this);
}

SpaceSwitcher::~SpaceSwitcher() {
  model_->RemoveObserver(this);
  if (tab_strip_model_) {
    Registry().erase(tab_strip_model_.get());
    tab_strip_model_->RemoveObserver(this);
  }
}

// static
SpaceSwitcher* SpaceSwitcher::FromTabStripModel(
    const TabStripModel* tab_strip_model) {
  auto it = Registry().find(tab_strip_model);
  return it == Registry().end() ? nullptr : it->second;
}

SpaceId SpaceSwitcher::SpaceOfTabAt(int index) const {
  if (!tab_strip_model_ || index < 0 || index >= tab_strip_model_->count()) {
    return SpaceId();
  }
  return SpaceOfTab(*model_, *binding_,
                    tab_strip_model_->GetTabAtIndex(index)->GetHandle());
}

bool SpaceSwitcher::IsInActiveSpace(int index) const {
  return SpaceOfTabAt(index) == active_space_;
}

std::vector<int> SpaceSwitcher::OpenTabsInSidebarOrder(SpaceId space) const {
  std::vector<int> result;
  if (!tab_strip_model_) {
    return result;
  }
  // Favourites, then pinned entries by position -- whichever of them
  // actually has an open tab in this strip -- mirroring the order
  // SidebarTabModel::rows() draws.
  for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
    for (const TabEntry* entry : model_->EntriesForKind(space, kind)) {
      std::optional<tabs::TabHandle> handle = binding_->TabForEntry(entry->id);
      tabs::TabInterface* tab = handle ? handle->Get() : nullptr;
      if (!tab) {
        continue;
      }
      const int index = tab_strip_model_->GetIndexOfTab(tab);
      if (index != TabStripModel::kNoTab) {
        result.push_back(index);
      }
    }
  }
  // Whatever no entry claims is Today, in strip order.
  const int count = tab_strip_model_->count();
  for (int i = 0; i < count; ++i) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(i);
    if (IsClaimedByEntry(*model_, *binding_, tab->GetHandle())) {
      continue;
    }
    if (SpaceOfTabAt(i) == space) {
      result.push_back(i);
    }
  }
  return result;
}

void SpaceSwitcher::SwitchTo(SpaceId id) {
  if (!model_->GetSpace(id) || !tab_strip_model_) {
    return;
  }
  if (id == active_space_) {
    return;
  }
  RecordActiveTab();
  active_space_ = id;
  model_->SetLastActiveSpace(id);

  // The tab to land on: the one left behind, then the space's first open
  // tab, then a new blank one. A space is never left showing another
  // space's page, which is the whole of §4.3's rule.
  //
  // `switching_` keeps the activation this causes from being read, back
  // through OnTabStripModelChanged, as the user choosing a foreign tab --
  // which would try to switch again while this call is still on the stack.
  base::AutoReset<bool> switching(&switching_, true);
  const std::vector<int> open = OpenTabsInSidebarOrder(id);
  int landing = -1;
  const TabKey last = model_->GetSpace(id)->last_active_tab;
  if (last.is_valid()) {
    for (int index : open) {
      // ExistingKeyOf, not KeyOf: this is a comparison over every open tab
      // of the space, and generating a UUID for each just to compare it
      // would be a side effect on a read.
      if (ExistingKeyOf(
              tab_strip_model_->GetTabAtIndex(index)->GetContents()) == last) {
        landing = index;
        break;
      }
    }
  }
  if (landing == -1 && !open.empty()) {
    landing = open.front();
  }
  if (landing == -1) {
    OpenBlankTab();
  } else {
    tab_strip_model_->ActivateTabAt(landing);
  }
  NotifyActiveSpaceChanged();
}

void SpaceSwitcher::RecordActiveTab() {
  const int index = tab_strip_model_->active_index();
  if (index == TabStripModel::kNoTab || !IsInActiveSpace(index)) {
    return;
  }
  model_->SetLastActiveTab(
      active_space_,
      KeyOf(tab_strip_model_->GetTabAtIndex(index)->GetContents()));
}

int SpaceSwitcher::OpenBlankTab() {
  if (!tab_strip_model_) {
    return TabStripModel::kNoTab;
  }
  // A plain WebContents with no navigation, not the delegate's AddTabAt --
  // that resolves an empty URL to the New Tab Page, and nothing about
  // landing on an empty space justifies loading a whole WebUI surface just
  // to give it something to show.
  std::unique_ptr<content::WebContents> contents = content::WebContents::Create(
      content::WebContents::CreateParams(tab_strip_model_->profile()));
  // Tagged before insertion, the same as any other tab, so TagInsertedTabs's
  // never-overwrite rule keeps this rather than whatever a Chromium-assigned
  // opener would suggest: AppendWebContents(foreground) always makes the
  // outgoing active tab this one's opener, and that tab belongs to the space
  // being left, not the one this blank tab is meant for.
  SetSpaceTag(contents.get(), active_space_);
  tab_strip_model_->AppendWebContents(std::move(contents), /*foreground=*/true);
  return tab_strip_model_->active_index();
}

void SpaceSwitcher::MoveTabToSpace(int index, SpaceId space) {
  if (!tab_strip_model_ || index < 0 || index >= tab_strip_model_->count() ||
      !model_->GetSpace(space)) {
    return;
  }
  SetSpaceTag(tab_strip_model_->GetTabAtIndex(index)->GetContents(), space);
  // Moving the tab you are looking at takes you with it, as Zen does.
  if (index == tab_strip_model_->active_index()) {
    SwitchTo(space);
  }
}

void SpaceSwitcher::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SpaceSwitcher::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SpaceSwitcher::NotifyActiveSpaceChanged() {
  for (Observer& observer : observers_) {
    observer.OnActiveSpaceChanged();
  }
}

void SpaceSwitcher::TagInsertedTabs(const TabStripModelChange::Insert& insert) {
  for (const auto& inserted : insert.contents) {
    if (!inserted.tab) {
      continue;
    }
    content::WebContents* contents = inserted.tab->GetContents();
    // A restored tab arrives already tagged, and a tag is never overwritten:
    // the session's answer is older and better than "wherever you are now".
    if (SpaceTagOf(contents).is_valid()) {
      continue;
    }
    // The tab strip's opener, which a link click and a Cmd+click both set,
    // and window.opener does not decide. A tab from another application has
    // none and joins the space on screen.
    tabs::TabInterface* opener =
        tab_strip_model_->GetOpenerOfTabAt(inserted.index);
    SetSpaceTag(contents,
                opener ? SpaceOfTab(*model_, *binding_, opener->GetHandle())
                       : active_space_);
  }
}

void SpaceSwitcher::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted) {
    if (const TabStripModelChange::Insert* insert = change.GetInsert()) {
      TagInsertedTabs(*insert);
    }
  }
  if (switching_ || !selection.active_tab_changed()) {
    return;
  }
  const int index = tab_strip_model_->active_index();
  if (index == TabStripModel::kNoTab) {
    return;
  }
  if (IsInActiveSpace(index)) {
    RecordActiveTab();
    return;
  }
  // §4.4: a tab from another space just became active -- most likely the
  // user clicked it directly in the strip. SwitchTo cannot run from here:
  // TabStripModel::ActivateTabAt and every other mutator hold a
  // ReentrancyCheck that this very callback runs inside, so calling it would
  // CHECK-crash, and it would also drag the user's selection back to
  // whatever tab this space last showed instead of leaving it where they put
  // it. So the window simply adopts the space the visible tab is already
  // in -- sets it, records it as last active, and tells observers -- without
  // touching the strip at all. Nothing needs to record the space being left:
  // RecordActiveTab already kept its last_active_tab current every time a
  // tab became active while it was still the one on screen.
  active_space_ = SpaceOfTabAt(index);
  model_->SetLastActiveSpace(active_space_);
  NotifyActiveSpaceChanged();
}

void SpaceSwitcher::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  Registry().erase(tab_strip_model);
  tab_strip_model_->RemoveObserver(this);
  tab_strip_model_ = nullptr;
}

void SpaceSwitcher::OnArciumModelChanged() {
  if (model_->GetSpace(active_space_)) {
    return;
  }
  // The space this window was showing is gone -- deleted from some window,
  // or (at launch) never real to begin with, because the window is built
  // before ModelStore::Load has run and starts out on the placeholder
  // model's only space. Either way something has to land the window on
  // last_active_space(), but this notification fires synchronously on every
  // model mutation, including ones RecordActiveTab makes from inside
  // OnTabStripModelChanged -- so a synchronous SwitchTo here could call
  // ActivateTabAt while the strip's own ReentrancyCheck is still held for
  // that very callback. Posting runs the fallback on its own turn, once
  // whatever the strip was doing has finished.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SpaceSwitcher::ApplyFallbackSwitch,
                                weak_factory_.GetWeakPtr()));
}

void SpaceSwitcher::ApplyFallbackSwitch() {
  // Re-checked because the model may have changed again -- another space
  // deletion, or a fresh AddSpace -- in the run-loop turns between the post
  // and this task running.
  if (!model_->GetSpace(active_space_)) {
    SwitchTo(model_->last_active_space());
  }
}

}  // namespace arcium
