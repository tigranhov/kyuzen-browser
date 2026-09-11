// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_ARCHIVE_SERVICE_H_
#define ARCIUM_UI_BROWSER_ARCHIVE_SERVICE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/archive_store.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/space.h"
#include "base/memory/raw_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tabs/public/tab_interface.h"

class TabStripModel;

namespace arcium {

class ArchiveStore;
class SpaceSwitcher;
class TabBinding;

// One read of the archive. `readable` is the store's own is_open(), captured
// on the store's sequence in the same task as the rows, because the two
// answers have to agree: an empty vector means "nothing archived" only when
// the file is actually open, and the caller cannot tell the difference from
// the rows alone.
struct ArchiveReadResult {
  ArchiveReadResult();
  ArchiveReadResult(ArchiveReadResult&&);
  ArchiveReadResult& operator=(ArchiveReadResult&&);
  ~ArchiveReadResult();

  std::vector<ArchivedTab> tabs;
  bool readable = false;
};

// Writes idle Today tabs to the archive and closes them, and reads it back
// for the archive list. Both halves are here because both need the same two
// things — the store and the sequence that owns it — and a second object
// holding the same pair would only be a second place to get that wrong.
//
// One service per window, because a tab is in exactly one strip: "the active
// tab in any window" is then just this strip's active tab, and two windows can
// never both decide to archive the same tab. The ArchiveStore behind it is
// per-profile — it is one SQLite file — and is shared by every window's
// service.
//
// One base::OneShotTimer for the whole service, aimed at the earliest expiry
// among the tabs it may archive, and restarted whenever that earliest expiry
// can have moved: a tab inserted, closed or switched away from, or the model
// changed (which is how a new timeout, a new pinned entry, or the completion of
// ModelStore::Load reaches us). Twenty tabs cost one timer, and a space set to
// kNever costs none — the timer is stopped, not aimed at a far-future date.
// Nothing here polls.
class ArchiveService : public TabStripModelObserver,
                       public ArciumModel::Observer {
 public:
  // `tab_strip_model`, `model` and `binding` must outlive this object.
  //
  // `store` is null off the record and when the archive file could not be
  // opened; archiving then closes tabs without recording them, which is what
  // incognito means and what a broken archive degrades to. It is touched only
  // on `store_runner`, never here: sql::Database blocks and is sequence-
  // affine, and this runs on the UI thread.
  //
  // `clock` answers the one question "what time is it for deciding whether a
  // tab has been idle long enough", and nothing else — the timestamps this
  // service writes into archive rows are always base::Time::Now(). Null means
  // base::Time::Now() for that question too, which is every window without
  // --arcium-fake-clock-offset. It must outlive this object;
  // BrowserSidebarController owns the one it passes.
  //
  // `switcher` is null in the playground, in a window built without a
  // sidebar, and in every fixture written before spaces -- all of which keep
  // working unchanged, in the first space, which is what they have always
  // meant by it. It must outlive this object, which registers itself with
  // it for as long as it lives.
  ArchiveService(TabStripModel* tab_strip_model,
                 ArciumModel* model,
                 TabBinding* binding,
                 ArchiveStore* store,
                 scoped_refptr<base::SequencedTaskRunner> store_runner,
                 const base::Clock* clock = nullptr,
                 SpaceSwitcher* switcher = nullptr);
  ArchiveService(const ArchiveService&) = delete;
  ArchiveService& operator=(const ArchiveService&) = delete;
  ~ArchiveService() override;

  // Archives and closes every Today tab, whatever its idle time. What the
  // divider's Clear button means, so it deliberately ignores the guards that
  // hold back the automatic sweep: the user asked.
  void ArchiveAllToday();

  // One reply from a read of the archive. Shared by every read this service
  // offers, because every one of them answers with the same pair: the rows,
  // and whether the file was readable at all.
  using ReadCallback = base::OnceCallback<void(ArchiveReadResult)>;

  // Whether this service was given a store at all. False off the record,
  // where there is no archive file and cannot be one, and true everywhere
  // else — including on a profile whose archive file will not open, because
  // ArciumProfileState keeps the ArchiveStore either way and the open is
  // posted, so at the moment the sidebar is built the answer is not yet
  // known. Whether the file opened is ArchiveReadResult::readable, which
  // travels back with the rows; this flag is only "is there an archive in
  // this window's world", which is what decides whether the button exists.
  bool has_store() const { return store_ != nullptr; }

  // The `limit` most recently archived tabs of `space_id`, newest first.
  //
  // Asynchronous because ArchiveStore is not: every one of its methods blocks
  // on SQLite, and a read whose pages are not in the OS cache is a disk seek.
  // Doing that from the click that opens the list would be a dropped frame
  // the user is looking at, and "no sync I/O ever" covers reads.
  //
  // `callback` runs on the calling sequence, and only ever on a later turn of
  // the run loop — including in the no-store case, so a caller never has to
  // handle being answered from inside its own call. It is dropped without
  // running if whatever it is bound to has gone by then; bind through a
  // WeakPtr.
  void RequestRecent(SpaceId space_id, int limit, ReadCallback callback);

  // The archived tabs matching `query`, newest first, over every space. This
  // is a pass-through, so `limit` carries ArchiveStore::Search's meaning and
  // not RequestRecent's: it bounds DISTINCT URLS, one row each, rather than
  // rows — a page archived repeatedly counts once.
  //
  // Asynchronous for exactly the reason RequestRecent is, and posted
  // onto the same sequence for exactly the reason it is: sql::Database is
  // sequence-affine, this service owns the one sequence it is bound to, and a
  // second async path over the same store would be a second place to get that
  // wrong. TabSearchService is the caller; ArchiveStore::Search does the
  // matching, on its folded shadow columns.
  void RequestSearch(const std::u16string& query,
                     int limit,
                     ReadCallback callback);

  // Drops one archived row. Both halves of the archive's primary key are
  // needed to name it. Fire-and-forget: nothing waits on the delete, and a
  // row that is already gone is not an error.
  void RemoveArchived(const GURL& url, base::Time archived_at);

  // Drops every archived row of `space_id`, posted to the store's sequence
  // like every other write here: a deleted space can have thousands of rows,
  // and the UI thread never waits for SQLite. Called by SpaceSwitcher when
  // the space itself is deleted; the space's entries and folders are the
  // model's to remove.
  void RemoveSpaceRows(SpaceId space_id);

  // False for the tabs closing would be data loss rather than tidying: the
  // active tab, a tab playing audio, a tab with an unload handler, and any tab
  // an entry still claims (a pinned or favourite tab is not a Today tab).
  bool MayArchive(tabs::TabHandle handle) const;

  std::optional<base::Time> next_expiry_for_testing() const {
    return next_expiry_;
  }
  // The contract is one live timer for the whole service, not one per tab.
  int live_timer_count_for_testing() const {
    return timer_.IsRunning() ? 1 : 0;
  }
  // Rows read for a close that has not completed yet. Keyed by handle, so
  // asking the same tab to close again while its first attempt is still
  // outstanding replaces its parked row instead of queuing a second one.
  size_t pending_archive_count_for_testing() const {
    return pending_archive_.size();
  }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // ArciumModel::Observer:
  void OnArciumModelChanged() override;

 private:
  // The space this window is showing. Without a switcher — the playground, a
  // window built with no sidebar, every fixture written before spaces — it is
  // the first space, which is what those callers have always meant.
  SpaceId active_space() const;
  // The instant `handle` becomes archivable, or nullopt when its space is set
  // to kNever.
  std::optional<base::Time> ExpiryFor(tabs::TabHandle handle) const;
  // The earliest expiry over the tabs MayArchive allows, or nullopt when there
  // is nothing to wait for.
  std::optional<base::Time> EarliestExpiry() const;
  // The instant `handle` stopped being visible, which is what its idle time is
  // measured from. See `last_active_`.
  base::Time IdleSince(tabs::TabHandle handle) const;

  // `handle`'s own space's timeout — not the window's active space, which may
  // differ from where a background tab actually sits.
  ArchiveTimeout TimeoutForTab(tabs::TabHandle handle) const;

  void RescheduleTimer();
  void OnTimerFired();
  // Reads `handle`'s row, parks it in `pending_archive_`, and asks the strip
  // to close the tab. The close may finish inside this call, or arbitrarily
  // later behind a beforeunload dialog, or never. The write happens only from
  // the kRemoved branch of OnTabStripModelChanged, which is the one place the
  // tab is known to be actually gone, and the row is stamped there rather than
  // here. A declined close drops its row at the tab's next page change; see
  // `pending_archive_`.
  // `close_types` says whose close this is: kUserCloseTypes for Clear, which
  // the user pressed, kSweepCloseTypes for the timer, which they did not. See
  // tab_close_types.h.
  void ArchiveAndClose(tabs::TabHandle handle, uint32_t close_types);

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<ArciumModel> model_;
  raw_ptr<TabBinding> binding_;
  raw_ptr<SpaceSwitcher> switcher_;
  // Owned by ArciumProfileState, which deletes it on `store_runner_` — so a
  // write posted here always runs before the deletion behind it.
  raw_ptr<ArchiveStore> store_;
  scoped_refptr<base::SequencedTaskRunner> store_runner_;
  // Read for "is this tab idle enough yet", never for a stored timestamp.
  raw_ptr<const base::Clock> clock_;

  // When a tab this service has watched stopped being visible. Only tabs the
  // strip has told us about hold an entry; every other tab — one restored into
  // the window, one the user has just opened, one dragged in from another
  // window — is measured from WebContents::GetLastActiveTime(), which is
  // truthful for all three. That is deliberately the only fallback: an idle
  // clock with two sources is an idle clock with two answers, and this feature
  // has already been bitten by exactly that.
  std::map<tabs::TabHandle, base::Time> last_active_;

  // A row already read for a tab whose close is still outstanding. It waits
  // here until OnTabStripModelChanged reports the tab actually removed, and is
  // dropped by OnTabChangedAt if the tab's page changes first — which is the
  // only sign the strip gives that the close was declined and the user went on
  // using the tab. Those are the only two ways out: a tab that neither goes
  // nor changes keeps its row, and the row still describes it. See
  // ArchiveAndClose().
  std::map<tabs::TabHandle, ArchivedTab> pending_archive_;

  base::OneShotTimer timer_;
  std::optional<base::Time> next_expiry_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_ARCHIVE_SERVICE_H_
