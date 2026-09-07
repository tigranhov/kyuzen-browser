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
class ModelStore;
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
// can have moved: a tab activated, inserted or closed, or the model changed
// (which is how a new timeout, a new pinned entry, or the completion of
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
  // `model_store` is null off the record and in tests. It supplies the restart
  // floor — see RestartFloor().
  ArchiveService(TabStripModel* tab_strip_model,
                 ArciumModel* model,
                 TabBinding* binding,
                 ArchiveStore* store,
                 scoped_refptr<base::SequencedTaskRunner> store_runner,
                 ModelStore* model_store);
  ArchiveService(const ArchiveService&) = delete;
  ArchiveService& operator=(const ArchiveService&) = delete;
  ~ArchiveService() override;

  // Restarts `handle`'s idle clock. Called for every activation the strip
  // reports, and public because the sidebar's own notion of "the user touched
  // this tab" is not always a strip selection change.
  void OnTabActivated(tabs::TabHandle handle);

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
  // When `handle` was last active. A tab that was already open when the
  // service was made has no stamp of its own and takes the restart floor.
  base::Time IdleSince(tabs::TabHandle handle) const;
  // The idle floor for tabs restored after a quit: the model's last save, the
  // last moment the browser knew about them. A browser closed overnight
  // therefore archives yesterday's Today tabs on launch, without any per-tab
  // timestamp having to survive the quit.
  //
  // ModelStore::Load is asynchronous, so at construction last_save_time() is
  // usually still null. It reads as startup until the load lands, which
  // archives nothing early; the load ends in an ArciumModel notification, and
  // OnArciumModelChanged reschedules against the real floor as soon as there
  // is one.
  base::Time RestartFloor() const;

  ArchiveTimeout TimeoutForDefaultSpace() const;

  void RescheduleTimer();
  void OnTimerFired();
  // Writes `tab` to the archive on the store's sequence, then closes it.
  void ArchiveAndClose(tabs::TabHandle handle);
  void WriteToArchive(tabs::TabInterface* tab);

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<ArciumModel> model_;
  raw_ptr<TabBinding> binding_;
  // Owned by ArciumProfileState, which deletes it on `store_runner_` — so a
  // write posted here always runs before the deletion behind it.
  raw_ptr<ArchiveStore> store_;
  scoped_refptr<base::SequencedTaskRunner> store_runner_;
  raw_ptr<ModelStore> model_store_;

  // When the service was made, the floor before a load has landed.
  const base::Time created_at_;
  // A null value means "no stamp of its own": the tab predates the service.
  std::map<tabs::TabHandle, base::Time> last_active_;

  base::OneShotTimer timer_;
  std::optional<base::Time> next_expiry_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_ARCHIVE_SERVICE_H_
