// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/archive_service.h"

#include <algorithm>
#include <utility>

#include "arcium/browser/archive_store.h"
#include "arcium/browser/entry_claim.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/model_store.h"
#include "arcium/browser/tab_binding.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"

namespace arcium {

namespace {

constexpr uint32_t kCloseTypes = TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB;

}  // namespace

ArchiveService::ArchiveService(
    TabStripModel* tab_strip_model,
    ArciumModel* model,
    TabBinding* binding,
    ArchiveStore* store,
    scoped_refptr<base::SequencedTaskRunner> store_runner,
    ModelStore* model_store)
    : tab_strip_model_(tab_strip_model),
      model_(model),
      binding_(binding),
      store_(store),
      store_runner_(std::move(store_runner)),
      model_store_(model_store),
      created_at_(base::Time::Now()) {
  // Tabs that already exist get no stamp: they are restored tabs, and the
  // restart floor is what their idle time is measured from. See RestartFloor().
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    last_active_[tab_strip_model_->GetTabAtIndex(i)->GetHandle()] =
        base::Time();
  }
  tab_strip_model_->AddObserver(this);
  model_->AddObserver(this);
  RescheduleTimer();
}

ArchiveService::~ArchiveService() {
  model_->RemoveObserver(this);
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void ArchiveService::OnTabActivated(tabs::TabHandle handle) {
  last_active_[handle] = base::Time::Now();
  RescheduleTimer();
}

void ArchiveService::ArchiveAllToday() {
  if (!tab_strip_model_) {
    return;
  }
  // Collect first: closing a tab mutates the strip underneath the loop.
  // A tab an entry claims is not a Today tab, whatever Chromium thinks of its
  // pinned state; everything else goes, including the active one. This is a
  // button the user pressed, not the automatic sweep.
  std::vector<tabs::TabHandle> today;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    const tabs::TabHandle handle =
        tab_strip_model_->GetTabAtIndex(i)->GetHandle();
    if (!IsClaimedByEntry(*model_, *binding_, handle)) {
      today.push_back(handle);
    }
  }
  for (const tabs::TabHandle& handle : today) {
    ArchiveAndClose(handle);
  }
  RescheduleTimer();
}

bool ArchiveService::MayArchive(tabs::TabHandle handle) const {
  if (!tab_strip_model_) {
    return false;
  }
  tabs::TabInterface* tab = handle.Get();
  if (!tab) {
    return false;  // Already closed; the handle is weak.
  }
  const int index = tab_strip_model_->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab) {
    return false;  // Another window's tab, and so another service's business.
  }
  if (index == tab_strip_model_->active_index()) {
    return false;
  }
  // Bound *and still held by an entry the model has*: a tab left bound to an
  // entry ReplaceAll removed is a Today tab, and archiving it is the only
  // thing that ever gets rid of it.
  if (IsClaimedByEntry(*model_, *binding_, handle)) {
    return false;
  }
  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    return false;
  }
  if (contents->IsCurrentlyAudible()) {
    return false;
  }
  // A page with a beforeunload or unload handler has something to say about
  // being closed. Closing it silently is how a feature that eats work gets
  // switched off.
  if (contents->NeedToFireBeforeUnloadOrUnloadEvents()) {
    return false;
  }
  return true;
}

void ArchiveService::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted) {
    const TabStripModelChange::Insert* insert = change.GetInsert();
    if (insert) {
      for (const auto& inserted : insert->contents) {
        if (inserted.tab) {
          last_active_[inserted.tab->GetHandle()] = base::Time::Now();
        }
      }
    }
  } else if (change.type() == TabStripModelChange::kRemoved) {
    const TabStripModelChange::Remove* remove = change.GetRemove();
    if (remove) {
      for (const TabStripModelChange::RemovedTab& removed : remove->contents) {
        if (removed.tab) {
          last_active_.erase(removed.tab->GetHandle());
        }
      }
    }
  }
  if (selection.active_tab_changed() && selection.new_tab) {
    last_active_[selection.new_tab->GetHandle()] = base::Time::Now();
  }
  RescheduleTimer();
}

void ArchiveService::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  tab_strip_model_->RemoveObserver(this);
  tab_strip_model_ = nullptr;
  timer_.Stop();
  next_expiry_.reset();
}

void ArchiveService::OnArciumModelChanged() {
  // A changed timeout, an entry that now claims a tab, an entry that stopped
  // claiming one — and the completion of ModelStore::Load, which is what makes
  // the restart floor real. All of them move the earliest expiry.
  RescheduleTimer();
}

base::Time ArchiveService::RestartFloor() const {
  const base::Time saved =
      model_store_ ? model_store_->last_save_time() : base::Time();
  return saved.is_null() ? created_at_ : saved;
}

base::Time ArchiveService::IdleSince(tabs::TabHandle handle) const {
  const auto it = last_active_.find(handle);
  if (it == last_active_.end() || it->second.is_null()) {
    return RestartFloor();
  }
  return it->second;
}

ArchiveTimeout ArchiveService::TimeoutForDefaultSpace() const {
  const SpaceId id = model_->default_space_id();
  for (const Space& space : model_->spaces()) {
    if (space.id == id) {
      return space.archive_timeout;
    }
  }
  return ArchiveTimeout::kTwelveHours;
}

std::optional<base::Time> ArchiveService::ExpiryFor(
    tabs::TabHandle handle) const {
  const std::optional<base::TimeDelta> timeout =
      ArchiveTimeoutToDelta(TimeoutForDefaultSpace());
  if (!timeout) {
    return std::nullopt;  // kNever.
  }
  return IdleSince(handle) + *timeout;
}

std::optional<base::Time> ArchiveService::EarliestExpiry() const {
  if (!tab_strip_model_) {
    return std::nullopt;
  }
  std::optional<base::Time> earliest;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    const tabs::TabHandle handle =
        tab_strip_model_->GetTabAtIndex(i)->GetHandle();
    if (!MayArchive(handle)) {
      continue;
    }
    const std::optional<base::Time> expiry = ExpiryFor(handle);
    if (!expiry) {
      return std::nullopt;  // kNever: nothing in this space ever expires.
    }
    if (!earliest || *expiry < *earliest) {
      earliest = expiry;
    }
  }
  return earliest;
}

void ArchiveService::RescheduleTimer() {
  next_expiry_ = EarliestExpiry();
  if (!next_expiry_) {
    timer_.Stop();
    return;
  }
  const base::TimeDelta delay =
      std::max(*next_expiry_ - base::Time::Now(), base::TimeDelta());
  timer_.Start(
      FROM_HERE, delay,
      base::BindOnce(&ArchiveService::OnTimerFired, base::Unretained(this)));
}

void ArchiveService::OnTimerFired() {
  if (!tab_strip_model_) {
    return;
  }
  const base::Time now = base::Time::Now();
  std::vector<tabs::TabHandle> expired;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    const tabs::TabHandle handle =
        tab_strip_model_->GetTabAtIndex(i)->GetHandle();
    const std::optional<base::Time> expiry = ExpiryFor(handle);
    if (MayArchive(handle) && expiry && *expiry <= now) {
      expired.push_back(handle);
    }
  }
  for (const tabs::TabHandle& handle : expired) {
    ArchiveAndClose(handle);
    // A close the strip declined to perform (an unload handler registered
    // between the check and here, a delegate that refused) must not leave the
    // tab expired, or the next reschedule would ask for a zero delay and the
    // timer would spin. Give it a fresh full timeout instead.
    if (handle.Get()) {
      last_active_[handle] = now;
    }
  }
  RescheduleTimer();
}

void ArchiveService::ArchiveAndClose(tabs::TabHandle handle) {
  tabs::TabInterface* tab = handle.Get();
  if (!tab) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  WriteToArchive(tab);
  tab_strip_model_->CloseWebContentsAt(index, kCloseTypes);
}

void ArchiveService::WriteToArchive(tabs::TabInterface* tab) {
  if (!store_ || !store_runner_) {
    return;  // Incognito, or an archive that would not open. Close anyway.
  }
  const tabs::TabData data = tabs::TabData::FromTabInterface(tab);
  if (!data.visible_url.is_valid()) {
    return;  // Nothing worth a row; still close the tab.
  }
  ArchivedTab row;
  row.url = data.visible_url;
  row.title = data.title;
  row.space_id = model_->default_space_id();
  row.archived_at = base::Time::Now();
  // sql::Database blocks and is sequence-affine. The store belongs to
  // `store_runner_` and is only ever touched there; base::Unretained is safe
  // because its owner deletes it on that same sequence, behind this task.
  store_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ArchiveStore::Add, base::Unretained(store_), row));
}

}  // namespace arcium
