// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The window's live tabs: the Today rows, the tab commands behind them, the
// archive the sweep writes them to, and the strip and model observation that
// keeps the list fresh. The other half of this class — the persistent
// entries and the folders that hold them — is in
// sidebar_tab_model_entries.cc.

#include "arcium/ui/browser/sidebar_tab_model.h"

#include <algorithm>
#include <utility>

#include "arcium/browser/entry_claim.h"
#include "arcium/browser/model/reorder_index.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/ui/browser/archive_service.h"
#include "arcium/ui/browser/tab_close_types.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "components/tabs/public/tab_alert.h"
#include "components/tabs/public/tab_interface.h"
#include "components/tabs/public/tab_network_state.h"

namespace arcium {

SidebarTabModel::SidebarTabModel(TabStripModel* tab_strip_model,
                                 ArciumModel* arcium_model,
                                 TabBinding* binding)
    : tab_strip_model_(tab_strip_model),
      arcium_model_(arcium_model),
      binding_(binding) {
  tab_strip_model_->AddObserver(this);
  arcium_model_->AddObserver(this);
}

SidebarTabModel::~SidebarTabModel() {
  arcium_model_->RemoveObserver(this);
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

std::vector<SidebarRow> SidebarTabModel::rows() const {
  std::vector<SidebarRow> rows;
  if (!tab_strip_model_) {
    return rows;
  }
  const SpaceId space = arcium_model_->default_space_id();
  for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
    for (const TabEntry* entry : arcium_model_->EntriesForKind(space, kind)) {
      rows.push_back(RowForEntry(*entry));
    }
  }
  // Whatever no entry claims is Today, in strip order.
  const int count = tab_strip_model_->count();
  for (int i = 0; i < count; ++i) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(i);
    if (IsClaimedByEntry(tab)) {
      continue;
    }
    rows.push_back(RowForTab(i, tab));
  }
  return rows;
}

bool SidebarTabModel::IsClaimedByEntry(tabs::TabInterface* tab) const {
  // The predicate itself lives in arcium/browser so ArchiveService uses the
  // same one: a tab bound to an entry the model no longer has must fall into
  // Today here *and* be archivable there.
  return arcium::IsClaimedByEntry(*arcium_model_, *binding_, tab->GetHandle());
}

SidebarRow SidebarTabModel::RowForTab(int index,
                                      tabs::TabInterface* tab) const {
  const tabs::TabData data = tabs::TabData::FromTabInterface(tab);
  SidebarRow row;
  row.tab_index = index;
  row.section = SidebarSection::kToday;
  row.title = data.title;
  row.favicon = data.favicon;
  row.is_active = index == tab_strip_model_->active_index();
  row.is_loading = data.network_state != tabs::TabNetworkState::kNone &&
                   !data.should_hide_throbber;
  row.is_audible = data.alert_state == tabs::TabAlert::kAudioPlaying;
  row.is_muted = data.alert_state == tabs::TabAlert::kAudioMuting;
  row.url = data.visible_url;
  return row;
}

void SidebarTabModel::ActivateTab(int tab_index) {
  if (tab_index >= 0 && tab_index < tab_strip_model_->count()) {
    tab_strip_model_->ActivateTabAt(tab_index);
  }
}

void SidebarTabModel::CloseTab(int tab_index) {
  if (tab_index >= 0 && tab_index < tab_strip_model_->count()) {
    tab_strip_model_->CloseWebContentsAt(tab_index, kUserCloseTypes);
  }
}

void SidebarTabModel::MoveTab(int from_index, int to_index) {
  const int count = tab_strip_model_->count();
  if (from_index < 0 || from_index >= count || to_index < 0 ||
      to_index >= count || from_index == to_index) {
    return;
  }
  tab_strip_model_->MoveWebContentsAt(from_index, to_index,
                                      /*select_after_move=*/false);
}

void SidebarTabModel::NewTab() {
  tab_strip_model_->delegate()->AddTabAt(GURL(), -1, /*foreground=*/true);
}

void SidebarTabModel::ClearToday() {
  if (archive_service_) {
    // The browser has an archive: clearing Today writes those tabs down
    // before closing them, which is the whole difference between Clear and
    // closing them by hand.
    archive_service_->ArchiveAllToday();
    return;
  }
  // No service: the playground and the model's own tests. Close from the end
  // so indices stay valid. A tab an entry claims is not a Today tab, whatever
  // Chromium thinks of its pinned state.
  for (int i = tab_strip_model_->count() - 1; i >= 0; --i) {
    if (!IsClaimedByEntry(tab_strip_model_->GetTabAtIndex(i))) {
      tab_strip_model_->CloseWebContentsAt(i, kUserCloseTypes);
    }
  }
}

int SidebarTabModel::TodayStripIndexForPosition(int position) const {
  if (position < 0 || !tab_strip_model_) {
    return -1;
  }
  int seen = 0;
  const int count = tab_strip_model_->count();
  for (int i = 0; i < count; ++i) {
    if (IsClaimedByEntry(tab_strip_model_->GetTabAtIndex(i))) {
      continue;
    }
    if (seen == position) {
      return i;
    }
    ++seen;
  }
  return -1;
}

void SidebarTabModel::MoveTabBeforeStripIndex(int from, int before) {
  const int count = tab_strip_model_->count();
  if (from < 0 || from >= count || count == 0) {
    return;
  }
  // `before` counts the strip as it looks now, with `from` still in it; a
  // negative one is "after everything", which is the end of the strip.
  const int to = std::clamp(
      before < 0 ? count - 1 : LiftThenInsertIndex(from, before), 0, count - 1);
  if (to != from) {
    tab_strip_model_->MoveWebContentsAt(from, to, /*select_after_move=*/false);
  }
}

void SidebarTabModel::SetArchiveService(ArchiveService* service) {
  archive_service_ = service;
}

void SidebarTabModel::SetArchiveTimeout(ArchiveTimeout timeout) {
  arcium_model_->SetArchiveTimeout(arcium_model_->default_space_id(), timeout);
}

ArchiveTimeout SidebarTabModel::archive_timeout() const {
  // See ArchiveService::TimeoutForDefaultSpace, which asks the same question
  // for the same space through the same accessor.
  const Space* space =
      arcium_model_->GetSpace(arcium_model_->default_space_id());
  return space ? space->archive_timeout : Space().archive_timeout;
}

bool SidebarTabModel::has_archive() const {
  // Two conditions, not one. Off the record there is no service at all —
  // BrowserSidebarController does not build one — and the playground and the
  // model's own tests run without one too. A service can also exist with no
  // store behind it, which is what a window over a context with no archive
  // file is; it has nothing to list and never will, so it gets no button.
  //
  // What this is NOT is "the archive can be read". The file is opened on a
  // background sequence and this is asked while the sidebar is being built,
  // so the answer would be a race. An archive that will not open keeps its
  // button and says so in the list; see ArchiveReadResult::readable.
  return archive_service_ && archive_service_->has_store();
}

void SidebarTabModel::RequestArchivedRows(int limit,
                                          ArchivedRowsCallback callback) {
  if (!archive_service_) {
    // The playground and the model's own tests. Posted, and through the same
    // weak pointer the real path uses, so this branch answers on the same
    // turn of the run loop and is dropped in the same circumstances. A rare
    // branch that behaves differently is the branch nobody tested.
    //
    // SequencedTaskRunner rather than SingleThreadTaskRunner, matching
    // ArchiveService::RequestRecent and SidebarView::OnArchiveListClosed:
    // nothing on this path needs thread affinity, only ordering.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&SidebarTabModel::DeliverArchivedRows,
                                  weak_factory_.GetWeakPtr(),
                                  std::move(callback), ArchiveReadResult()));
    return;
  }
  archive_service_->RequestRecent(
      arcium_model_->default_space_id(), limit,
      base::BindOnce(&SidebarTabModel::DeliverArchivedRows,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void SidebarTabModel::DeliverArchivedRows(ArchivedRowsCallback callback,
                                          ArchiveReadResult result) {
  std::vector<ArchivedRow> rows;
  rows.reserve(result.tabs.size());
  for (ArchivedTab& tab : result.tabs) {
    ArchivedRow row;
    row.url = std::move(tab.url);
    // A page that never got a title archives with an empty one, and a blank
    // row is unclickable-looking. The URL is what the sidebar draws for a
    // cold entry in the same situation.
    row.title = tab.title.empty() ? base::UTF8ToUTF16(row.url.spec())
                                  : std::move(tab.title);
    row.archived_at = tab.archived_at;
    rows.push_back(std::move(row));
  }
  std::move(callback).Run(std::move(rows), result.readable);
}

void SidebarTabModel::ReopenArchived(const GURL& url, base::Time archived_at) {
  if (!tab_strip_model_ || !url.is_valid()) {
    return;
  }
  // Foreground: this is a click on the page the user asked for, not a
  // rearrangement. Deliberately bound to nothing — an archived tab was a
  // Today tab and comes back as one.
  tab_strip_model_->delegate()->AddTabAt(url, -1, /*foreground=*/true);
  if (archive_service_) {
    // The row goes whether or not the tab opened cleanly. Leaving it would
    // mean a list that grows a duplicate every time it is used, and the page
    // is in the strip now either way.
    archive_service_->RemoveArchived(url, archived_at);
  }
}

void SidebarTabModel::AddObserver(SidebarModel::Observer* observer) {
  observers_.AddObserver(observer);
}

void SidebarTabModel::RemoveObserver(SidebarModel::Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SidebarTabModel::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted &&
      pending_bind_.is_valid()) {
    const TabStripModelChange::Insert* insert = change.GetInsert();
    if (insert && !insert->contents.empty() && insert->contents.front().tab) {
      binding_->Bind(pending_bind_, insert->contents.front().tab->GetHandle());
      pending_bind_ = EntryId();
    }
  } else if (change.type() == TabStripModelChange::kRemoved) {
    const TabStripModelChange::Remove* remove = change.GetRemove();
    if (remove) {
      for (const TabStripModelChange::RemovedTab& removed : remove->contents) {
        // A tab moving to another window keeps its entry; a tab going away
        // releases it, which is what leaves the entry cold rather than
        // deleting it.
        if (removed.tab && removed.remove_reason !=
                               TabRemovedReason::kInsertedIntoOtherTabStrip) {
          binding_->UnbindTab(removed.tab->GetHandle());
        }
      }
    }
  }
  NotifyChanged();
}

void SidebarTabModel::OnTabChangedAt(tabs::TabInterface* tab,
                                     int index,
                                     TabChangeType change_type) {
  // Opening one tab reaches here twice: once for the insert, and again about
  // 200ms later when Browser::ProcessPendingUIUpdates flushes the deferred
  // title as TabChangeType::kAll. A test that counts notifications will see
  // one or two depending on how fast the tests before it ran, so it must call
  // MakeBrowserUiUpdatesImmediate() (arcium/test/sidebar_tab_model_unittest.cc)
  // to collapse Browser's 200ms coalescing window first.
  NotifyChanged();
}

void SidebarTabModel::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                              int index) {
  NotifyChanged();
}

void SidebarTabModel::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  tab_strip_model_->RemoveObserver(this);
  tab_strip_model_ = nullptr;
}

void SidebarTabModel::OnArciumModelChanged() {
  if (suppress_model_notifications_) {
    return;
  }
  NotifyChanged();
}

void SidebarTabModel::NotifyChanged() {
  if (notification_pending_) {
    return;
  }
  notification_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarTabModel::FlushNotification,
                                weak_factory_.GetWeakPtr()));
}

void SidebarTabModel::FlushNotification() {
  notification_pending_ = false;
  {
    // The write-back is part of this burst, not a new one.
    base::AutoReset<bool> suppress(&suppress_model_notifications_, true);
    SyncEntryTitles();
  }
  for (SidebarModel::Observer& observer : observers_) {
    observer.OnSidebarModelChanged();
  }
}

}  // namespace arcium
