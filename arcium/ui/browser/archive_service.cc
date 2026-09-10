// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/archive_service.h"

#include <algorithm>
#include <string>
#include <utility>

#include "arcium/browser/archive_store.h"
#include "arcium/browser/entry_claim.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/browser/tab_close_types.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/default_clock.h"
#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"

namespace arcium {

namespace {

// The row `tab` would be archived as, or nullopt when there is nothing worth
// recording. Read before the close, because afterwards the tab is gone.
//
// `archived_at` is deliberately left unset here and stamped where the row is
// written, which can be arbitrarily later: a close held behind a dialog is
// archived when the tab actually went, not when the button was pressed, and
// ListRecent orders by that field. It is base::Time::Now() and never the
// service's clock — a row records when it was really written, whatever
// --arcium-fake-clock-offset has told the sweep to believe. A free function
// so there is no clock here to reach for by accident.
std::optional<ArchivedTab> MakeRow(tabs::TabInterface* tab, SpaceId space_id) {
  const tabs::TabData data = tabs::TabData::FromTabInterface(tab);
  if (!data.visible_url.is_valid()) {
    return std::nullopt;
  }
  ArchivedTab row;
  row.url = data.visible_url;
  row.title = data.title;
  row.space_id = space_id;
  return row;
}

// Runs on the store's sequence. Both halves of the answer are read in the one
// task on purpose: is_open() asked from the UI thread would be a race, and
// asked in a second posted task it could disagree with the rows it is meant
// to explain.
ArchiveReadResult ReadRecent(ArchiveStore* store, SpaceId space_id, int limit) {
  ArchiveReadResult result;
  result.readable = store->is_open();
  if (result.readable) {
    result.tabs = store->ListRecent(space_id, limit);
  }
  return result;
}

// Runs on the store's sequence, and reads both halves in the one task for the
// same reason ReadRecent does.
ArchiveReadResult ReadSearch(ArchiveStore* store,
                             const std::u16string& query,
                             int limit) {
  ArchiveReadResult result;
  result.readable = store->is_open();
  if (result.readable) {
    result.tabs = store->Search(query, limit);
  }
  return result;
}

}  // namespace

ArchiveReadResult::ArchiveReadResult() = default;
ArchiveReadResult::ArchiveReadResult(ArchiveReadResult&&) = default;
ArchiveReadResult& ArchiveReadResult::operator=(ArchiveReadResult&&) = default;
ArchiveReadResult::~ArchiveReadResult() = default;

ArchiveService::ArchiveService(
    TabStripModel* tab_strip_model,
    ArciumModel* model,
    TabBinding* binding,
    ArchiveStore* store,
    scoped_refptr<base::SequencedTaskRunner> store_runner,
    const base::Clock* clock,
    SpaceSwitcher* switcher)
    : tab_strip_model_(tab_strip_model),
      model_(model),
      binding_(binding),
      switcher_(switcher),
      store_(store),
      store_runner_(std::move(store_runner)),
      clock_(clock ? clock : base::DefaultClock::GetInstance()) {
  // No seeding loop. BrowserView builds this inside its own constructor, when
  // the strip is empty in every real launch — session restore, the startup NTP
  // and command-line URLs all insert their tabs afterwards — so a loop over
  // existing tabs is a path production never takes, and a path production never
  // takes is a path that drifts. Whatever tabs are here, IdleSince() measures
  // them the same way it measures the ones that arrive later.
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

SpaceId ArchiveService::active_space() const {
  return switcher_ ? switcher_->active_space() : model_->default_space_id();
}

void ArchiveService::ArchiveAllToday() {
  if (!tab_strip_model_) {
    return;
  }
  // Collect first: closing a tab mutates the strip underneath the loop.
  // A tab an entry claims is not a Today tab, whatever Chromium thinks of its
  // pinned state; everything else in this window's own space goes, including
  // the active one — a tab of another space is not this window's Today to
  // clear. This is a button the user pressed, not the automatic sweep.
  const SpaceId space = active_space();
  std::vector<tabs::TabHandle> today;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    const tabs::TabHandle handle =
        tab_strip_model_->GetTabAtIndex(i)->GetHandle();
    if (!IsClaimedByEntry(*model_, *binding_, handle) &&
        SpaceOfTab(*model_, *binding_, handle) == space) {
      today.push_back(handle);
    }
  }
  for (const tabs::TabHandle& handle : today) {
    ArchiveAndClose(handle, kUserCloseTypes);
  }
  RescheduleTimer();
}

void ArchiveService::RequestRecent(SpaceId space_id,
                                   int limit,
                                   ReadCallback callback) {
  if (!store_ || !store_runner_) {
    // Posted rather than run here. A caller that is answered from inside its
    // own call has a second order of events to be correct in, and this branch
    // is the rare one, so it would be the one nobody tested.
    //
    // `readable` stays false: there is no archive here to be empty. Off the
    // record nothing reaches this — the button does not exist — so the only
    // callers are the playground and the tests.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), ArchiveReadResult()));
    return;
  }
  // base::Unretained for the same reason Add's is safe: the store belongs to
  // `store_runner_`, is only ever touched there, and its owner deletes it on
  // that same sequence behind every task already posted. The reply runs here
  // and is the caller's to keep alive, or not.
  store_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ReadRecent, base::Unretained(store_), space_id, limit),
      std::move(callback));
}

void ArchiveService::RequestSearch(const std::u16string& query,
                                   int limit,
                                   ReadCallback callback) {
  if (!store_ || !store_runner_) {
    // Posted rather than run here, for the reason RequestRecent's own no-store
    // branch is.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), ArchiveReadResult()));
    return;
  }
  // base::Unretained for the same reason RequestRecent's is safe: the store
  // belongs to `store_runner_`, is only ever touched there, and its owner
  // deletes it on that same sequence behind every task already posted.
  store_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ReadSearch, base::Unretained(store_), query, limit),
      std::move(callback));
}

void ArchiveService::RemoveArchived(const GURL& url, base::Time archived_at) {
  if (!store_ || !store_runner_) {
    return;
  }
  store_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ArchiveStore::Remove, base::Unretained(store_),
                                url, archived_at));
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
  // Never the tab a switch to that space would land on. The window's own
  // active tab is refused above; this is the same rule for the spaces that
  // are not on screen, and without it a quiet background space loses the
  // page it was left on.
  for (const Space& space : model_->spaces()) {
    if (space.last_active_tab.is_valid() &&
        space.last_active_tab == ExistingKeyOf(contents)) {
      return false;
    }
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
  // Nothing is stamped on insert: a tab arriving from session restore already
  // carries the last-active time saved in the session, and one the user has
  // just opened carries its creation time. Both are read by IdleSince().
  if (change.type() == TabStripModelChange::kRemoved) {
    const TabStripModelChange::Remove* remove = change.GetRemove();
    if (remove) {
      for (const TabStripModelChange::RemovedTab& removed : remove->contents) {
        if (!removed.tab) {
          continue;
        }
        const tabs::TabHandle handle = removed.tab->GetHandle();
        last_active_.erase(handle);
        // The one place a tab this service asked to close is known to be
        // actually gone, whether the close finished inline or arrived later
        // through a confirmation the user gave. See ArchiveAndClose().
        const auto pending = pending_archive_.find(handle);
        if (pending == pending_archive_.end()) {
          continue;
        }
        ArchivedTab row = pending->second;
        pending_archive_.erase(pending);
        // Stamped here rather than at the press. See MakeRow().
        row.archived_at = base::Time::Now();
        // sql::Database blocks and is sequence-affine. The store belongs to
        // `store_runner_` and is only ever touched there; base::Unretained is
        // safe because its owner deletes it on that same sequence, behind
        // this task.
        if (store_ && store_runner_) {
          store_runner_->PostTask(
              FROM_HERE,
              base::BindOnce(&ArchiveStore::Add, base::Unretained(store_),
                             std::move(row)));
        }
      }
    }
  }
  // A tab's idle clock starts when it stops being visible, not when it was
  // activated. Stamping only the new tab meant a tab that had been on screen
  // for thirteen hours was archived the instant the user opened another one —
  // the tab they were reading a moment ago, which is the worst thing this
  // feature can do.
  //
  // Not when the old tab is the one being closed — it is already detached by
  // the time this runs, and stamping it would put back the entry the kRemoved
  // branch above just erased. TabStripModel guards its own use of `old_tab`
  // the same way.
  if (selection.active_tab_changed() && selection.old_tab &&
      tab_strip_model->GetIndexOfTab(selection.old_tab) !=
          TabStripModel::kNoTab) {
    last_active_[selection.old_tab->GetHandle()] = base::Time::Now();
  }
  RescheduleTimer();
}

void ArchiveService::OnTabChangedAt(tabs::TabInterface* tab,
                                    int index,
                                    TabChangeType change_type) {
  // The strip says nothing when a close is *declined* — a beforeunload dialog
  // the user cancels, a TabUnloadHandler's confirmation they answer no to. It
  // does say when the tab's page changes afterwards, and only a tab the user
  // kept can do that. The row parked against it was read at the press and
  // describes a page that is no longer open, so it goes.
  //
  // Without this the parking is unbounded: the row waits against that tab for
  // the rest of its life, and a hand-close hours later writes it — archiving
  // a Today tab nothing asked to archive, under a URL and title that have
  // moved on. kAll is the committed change (a new page, a new title), not the
  // loading flicker a close of its own can produce.
  if (tab && change_type == TabChangeType::kAll) {
    pending_archive_.erase(tab->GetHandle());
  }
}

void ArchiveService::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  tab_strip_model_->RemoveObserver(this);
  tab_strip_model_ = nullptr;
  timer_.Stop();
  next_expiry_.reset();
}

void ArchiveService::OnArciumModelChanged() {
  // A changed timeout, an entry that now claims a tab, an entry that stopped
  // claiming one — and the completion of ModelStore::Load, which replaces the
  // entries wholesale. All of them move the earliest expiry.
  RescheduleTimer();
}

base::Time ArchiveService::IdleSince(tabs::TabHandle handle) const {
  const auto it = last_active_.find(handle);
  if (it != last_active_.end()) {
    return it->second;
  }
  // A tab this service has not watched stop being visible. The tab itself
  // knows when it was last on screen, and for a tab restored after a quit that
  // answer survived the quit: session restore reads the saved last-active time
  // out of the session file and hands it to WebContents::CreateParams, so a
  // browser closed overnight really does archive yesterday's Today tabs on
  // launch. For a tab the user opened a moment ago it is that tab's creation
  // time, which is what "opened a moment ago" should mean. One rule, both
  // cases, no way to tell a restored tab from a fresh one and no need to.
  tabs::TabInterface* tab = handle.Get();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  return contents ? contents->GetLastActiveTime() : base::Time::Now();
}

ArchiveTimeout ArchiveService::TimeoutForTab(tabs::TabHandle handle) const {
  // A model with no space at all is a model mid-construction; Space's own
  // default is the honest answer, and it is the only place twelve hours is
  // written down. `handle`'s own space, not the window's active one: a
  // background tab of a space this window is not showing still ages against
  // that space's own timeout.
  const Space* space = model_->GetSpace(SpaceOfTab(*model_, *binding_, handle));
  return space ? space->archive_timeout : Space().archive_timeout;
}

std::optional<base::Time> ArchiveService::ExpiryFor(
    tabs::TabHandle handle) const {
  const std::optional<base::TimeDelta> timeout =
      ArchiveTimeoutToDelta(TimeoutForTab(handle));
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
      // kNever: this tab's own space never expires, but a strip holds every
      // space's tabs, and another one may still have a timeout to honour.
      continue;
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
  // The service's clock, not base::Time::Now(): under
  // --arcium-fake-clock-offset the expiry is already behind us and this
  // collapses to zero, which is how the switch makes the sweep immediate.
  const base::TimeDelta delay =
      std::max(*next_expiry_ - clock_->Now(), base::TimeDelta());
  timer_.Start(
      FROM_HERE, delay,
      base::BindOnce(&ArchiveService::OnTimerFired, base::Unretained(this)));
}

void ArchiveService::OnTimerFired() {
  if (!tab_strip_model_) {
    return;
  }
  // Both the comparison below and the retry stamp under it are in the
  // service's own clock, so the two agree: a declined close restamped in real
  // time would still read as expired under an offset clock, and the timer
  // would spin on it.
  const base::Time now = clock_->Now();
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
    ArchiveAndClose(handle, kSweepCloseTypes);
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

void ArchiveService::ArchiveAndClose(tabs::TabHandle handle,
                                     uint32_t close_types) {
  tabs::TabInterface* tab = handle.Get();
  if (!tab) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  // Read the row first — after the close the tab is gone — but park it rather
  // than writing it. A close can be declined (a beforeunload dialog the user
  // cancels, a TabUnloadHandler with its own confirmation), and it can also be
  // confirmed long after this call returns. Deciding here, synchronously, gets
  // one of those two wrong whichever way it is written: checking whether the
  // tab survived this call missed the confirmed-later case entirely. Only the
  // kRemoved branch knows.
  const std::optional<ArchivedTab> row =
      MakeRow(tab, SpaceOfTab(*model_, *binding_, handle));
  if (row) {
    // Keyed by handle: pressing Clear again while the first close is still
    // waiting on a dialog replaces this row rather than queuing a second, so
    // at most one row is ever written for the tab.
    pending_archive_[handle] = *row;
  }
  tab_strip_model_->CloseWebContentsAt(index, close_types);
}

}  // namespace arcium
