// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/space_switcher.h"

#include <algorithm>
#include <map>

#include "arcium/browser/entry_claim.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/session_rebuild_nudge.h"
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

// Erases `tab_strip_model`'s entry only when it names `switcher`, so no
// switcher can take another's registration with it.
void Unregister(const TabStripModel* tab_strip_model,
                const SpaceSwitcher* switcher) {
  auto it = Registry().find(tab_strip_model);
  if (it != Registry().end() && it->second == switcher) {
    Registry().erase(it);
  }
}

}  // namespace

SpaceSwitcher::SpaceSwitcher(TabStripModel* tab_strip_model,
                             ArciumModel* model,
                             TabBinding* binding)
    : tab_strip_model_(tab_strip_model),
      model_(model),
      binding_(binding),
      active_space_(model->last_active_space()) {
  // One switcher per strip. A second would silently fail to register, and
  // every hooked command and close would then quietly fall back to
  // Chromium's whole-strip behaviour once the first one went.
  const bool registered =
      Registry().emplace(tab_strip_model_.get(), this).second;
  CHECK(registered);
  tab_strip_model_->AddObserver(this);
  model_->AddObserver(this);
  // Through this window's own strip, so whoever builds the switcher has
  // nothing more to wire. Off the record there is no session file to keep in
  // step, and asking must not be what creates a SessionService for one.
  Profile* profile = tab_strip_model_->profile();
  if (profile && !profile->IsOffTheRecord()) {
    session_rebuild_request_ =
        base::BindRepeating(&RequestSessionRebuild, base::Unretained(profile));
  }
}

SpaceSwitcher::~SpaceSwitcher() {
  model_->RemoveObserver(this);
  if (tab_strip_model_) {
    Unregister(tab_strip_model_.get(), this);
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

bool SpaceSwitcher::IsClaimedByEntryAt(int index) const {
  if (!tab_strip_model_ || index < 0 || index >= tab_strip_model_->count()) {
    return false;
  }
  return IsClaimedByEntry(*model_, *binding_,
                          tab_strip_model_->GetTabAtIndex(index)->GetHandle());
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
  // The activation below re-enters OnTabStripModelChanged, but active_space_
  // already names `id` by then, so the landing tab -- one of `id`'s own open
  // tabs, or the blank tab InsertBlankTab tags into `id` before inserting it --
  // reads as in-space there. That runs RecordActiveTab instead of the
  // foreign-tab branch, which is exactly what should record this space's new
  // place; nothing here needs to suppress it.
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
  const bool lands_on_blank = landing == -1;
  if (lands_on_blank) {
    InsertBlankTab();
  } else {
    tab_strip_model_->ActivateTabAt(landing);
  }
  NotifyActiveSpaceChanged();
  // After the notification rather than from inside the insert: whatever the
  // callback shows over the blank tab asks which space the window is in, and
  // every observer has to have heard of the switch by then.
  if (lands_on_blank && blank_tab_callback_) {
    blank_tab_callback_.Run();
  }
}

void SpaceSwitcher::RecordActiveTab() {
  const int index = tab_strip_model_->active_index();
  if (index == TabStripModel::kNoTab || !IsInActiveSpace(index)) {
    return;
  }
  content::WebContents* contents =
      tab_strip_model_->GetTabAtIndex(index)->GetContents();
  // Asked before KeyOf generates one: a key the tab already had was written
  // by whichever rebuild followed its generation, and asking again on every
  // activation would rebuild the session on every tab click.
  const bool generated = !ExistingKeyOf(contents).is_valid();
  model_->SetLastActiveTab(active_space_, KeyOf(contents));
  if (generated) {
    AskForSessionRebuild();
  }
}

void SpaceSwitcher::AskForSessionRebuild() {
  // Posted and coalesced by the nudge, so a burst -- twenty tabs restored,
  // a delete re-tagging several -- costs one rebuild. The rebuild itself is
  // not free: ScheduleResetCommands walks every tab of every window once. That
  // is the trade the pin nudge already makes (InstallSessionRebuildNudge),
  // accepted for the same reason: without it a tab's space and the key a
  // space lands on reach the file only when something unrelated happens to
  // rebuild it, so a quit a minute after opening tabs in a space brings them
  // back in the first one.
  if (session_rebuild_request_) {
    session_rebuild_request_.Run();
  }
}

void SpaceSwitcher::AdoptSpace(SpaceId id) {
  // Shared by the foreign-activation branch of OnTabStripModelChanged and by
  // MoveTabToSpace: both put the window in a space it did not SwitchTo, and
  // both need the tab already on screen recorded as that space's place --
  // otherwise a later switch or a further adoption lands on whatever was
  // recorded before, not on the tab the user is actually looking at.
  active_space_ = id;
  model_->SetLastActiveSpace(id);
  RecordActiveTab();
  NotifyActiveSpaceChanged();
}

int SpaceSwitcher::OpenBlankTab() {
  const int index = InsertBlankTab();
  // After the append has finished rather than from the strip's observer
  // callback: whatever the callback shows sits over a tab that is already
  // on screen.
  if (index != TabStripModel::kNoTab && blank_tab_callback_) {
    blank_tab_callback_.Run();
  }
  return index;
}

bool SpaceSwitcher::OpenBlankTabBeforeClosing(const std::vector<int>& closing) {
  if (!tab_strip_model_) {
    return false;
  }
  const int active = tab_strip_model_->active_index();
  if (active == TabStripModel::kNoTab || !IsInActiveSpace(active) ||
      std::ranges::find(closing, active) == closing.end()) {
    return false;
  }
  // Chromium's own pick is right whenever the space keeps another open tab,
  // because NextSelectedIndexInSpace hands it that one. It is wrong for the
  // space's last: Chromium would activate a tab from another space, or close
  // the window outright. So that case opens the blank tab before anything
  // closes, and the window never shows a page from somewhere else.
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    if (IsInActiveSpace(index) &&
        std::ranges::find(closing, index) == closing.end()) {
      return false;
    }
  }
  OpenBlankTab();
  return true;
}

int SpaceSwitcher::InsertBlankTab() {
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

void SpaceSwitcher::SetBlankTabCallback(base::RepeatingClosure callback) {
  blank_tab_callback_ = std::move(callback);
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
    const SpaceId space =
        opener ? SpaceOfTab(*model_, *binding_, opener->GetHandle())
               : active_space_;
    SetSpaceTag(contents, space);
    // A tab no rebuild has written yet restores into the first space anyway,
    // so a new tab tagged with the first space leaves nothing to write. Only
    // a new tab: one moved into the first space may have been written as
    // another, and MoveTabToSpace asks whatever the target.
    if (space != model_->default_space_id()) {
      AskForSessionRebuild();
    }
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
  if (!selection.active_tab_changed()) {
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
  // it. AdoptSpace puts the window in the visible tab's space and records
  // that tab as its place, without touching the strip.
  AdoptSpace(SpaceOfTabAt(index));
}

void SpaceSwitcher::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  Unregister(tab_strip_model, this);
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
  if (model_->GetSpace(active_space_) || !tab_strip_model_) {
    return;
  }
  const SpaceId target = model_->last_active_space();
  // At launch the tab on screen is the one session restore selected, which
  // is where the user was at quit. When it is already in the space that was
  // active then, the window only has to take that space: SwitchTo would land
  // on whatever the space recorded, or on its first open tab, and throw the
  // restored selection away.
  const int active = tab_strip_model_->active_index();
  if (active != TabStripModel::kNoTab && SpaceOfTabAt(active) == target) {
    AdoptSpace(target);
    return;
  }
  SwitchTo(target);
}

}  // namespace arcium
