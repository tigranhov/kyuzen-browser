// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_ARCHIVE_SERVICE_H_
#define ARCIUM_UI_BROWSER_ARCHIVE_SERVICE_H_

#include <map>
#include <optional>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/space.h"
#include "base/memory/raw_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tabs/public/tab_interface.h"

class TabStripModel;

namespace arcium {

class ArchiveStore;
class TabBinding;

// Writes idle Today tabs to the archive and closes them.
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
  ArchiveService(TabStripModel* tab_strip_model,
                 ArciumModel* model,
                 TabBinding* binding,
                 ArchiveStore* store,
                 scoped_refptr<base::SequencedTaskRunner> store_runner);
  ArchiveService(const ArchiveService&) = delete;
  ArchiveService& operator=(const ArchiveService&) = delete;
  ~ArchiveService() override;

  // Archives and closes every Today tab, whatever its idle time. What the
  // divider's Clear button means, so it deliberately ignores the guards that
  // hold back the automatic sweep: the user asked.
  void ArchiveAllToday();

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

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // ArciumModel::Observer:
  void OnArciumModelChanged() override;

 private:
  // The instant `handle` becomes archivable, or nullopt when its space is set
  // to kNever.
  std::optional<base::Time> ExpiryFor(tabs::TabHandle handle) const;
  // The earliest expiry over the tabs MayArchive allows, or nullopt when there
  // is nothing to wait for.
  std::optional<base::Time> EarliestExpiry() const;
  // The instant `handle` stopped being visible, which is what its idle time is
  // measured from. See `last_active_`.
  base::Time IdleSince(tabs::TabHandle handle) const;

  ArchiveTimeout TimeoutForDefaultSpace() const;

  void RescheduleTimer();
  void OnTimerFired();
  // Closes `handle`'s tab and, only once the close has actually happened,
  // writes it to the archive on the store's sequence.
  void ArchiveAndClose(tabs::TabHandle handle);

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<ArciumModel> model_;
  raw_ptr<TabBinding> binding_;
  // Owned by ArciumProfileState, which deletes it on `store_runner_` — so a
  // write posted here always runs before the deletion behind it.
  raw_ptr<ArchiveStore> store_;
  scoped_refptr<base::SequencedTaskRunner> store_runner_;

  // When a tab this service has watched stopped being visible. Only tabs the
  // strip has told us about hold an entry; every other tab — one restored into
  // the window, one the user has just opened, one dragged in from another
  // window — is measured from WebContents::GetLastActiveTime(), which is
  // truthful for all three. That is deliberately the only fallback: an idle
  // clock with two sources is an idle clock with two answers, and this feature
  // has already been bitten by exactly that.
  std::map<tabs::TabHandle, base::Time> last_active_;

  base::OneShotTimer timer_;
  std::optional<base::Time> next_expiry_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_ARCHIVE_SERVICE_H_
