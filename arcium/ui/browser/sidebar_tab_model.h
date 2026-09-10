// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_
#define ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/browser/cold_favicon_cache.h"
#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class TabStripModel;

namespace tabs {
class TabInterface;
}

namespace arcium {

class ArchiveService;
struct ArchiveReadResult;
struct ArchivedTab;
struct TabEntry;

// Merges the window's live tabs with the profile's persistent entries into
// one list of rows. Favourites lead, then pinned entries in `position` order
// whether or not they have a tab, then whatever tabs no entry claims.
//
// The asymmetry is deliberate: an entry owns identity and outlives its tab,
// so closing a warm entry's tab leaves the entry cold rather than deleting
// it, while clicking a cold entry opens its URL and binds the new tab.
//
// Nothing is cached per row. Strip callbacks arrive in bursts (insert, then
// title, favicon and loading updates for the same tab) and an ArciumModel
// mutation is another source of the same burst; all of them are coalesced
// into one observer notification per run-loop turn, posted as a task. No
// timers.
class SidebarTabModel : public SidebarModel,
                        public TabStripModelObserver,
                        public ArciumModel::Observer,
                        public SpaceSwitcher::Observer {
 public:
  // `arcium_model` and `binding` are shared by every window on the profile
  // and must outlive this object. `switcher` is null in the playground, in a
  // window built without a sidebar, and in every fixture written before
  // spaces -- all of which keep working unchanged, in the first space, which
  // is what they have always meant by it. It must outlive this object.
  SidebarTabModel(TabStripModel* tab_strip_model,
                  ArciumModel* arcium_model,
                  TabBinding* binding,
                  SpaceSwitcher* switcher = nullptr);
  SidebarTabModel(const SidebarTabModel&) = delete;
  SidebarTabModel& operator=(const SidebarTabModel&) = delete;
  ~SidebarTabModel() override;

  // The window's ArchiveService, or null in the tests and the playground that
  // have none. Set once by BrowserSidebarController, which owns both: the
  // service needs a built strip and this model does not, so it cannot be a
  // constructor argument. Only ClearToday() reads it.
  void SetArchiveService(ArchiveService* service);

  // SidebarModel:
  std::vector<SidebarRow> rows() const override;
  void ActivateTab(int tab_index) override;
  void CloseTab(int tab_index) override;
  void MoveTab(int from_index, int to_index) override;
  void NewTab() override;
  void ClearToday() override;
  void AddToFavorites(int tab_index) override;
  void PinTab(int tab_index) override;
  void MoveTabToSection(int tab_index,
                        SidebarSection section,
                        int position) override;
  void UnpinEntry(EntryId id) override;
  void ActivateEntry(EntryId id) override;
  void CloseEntryTab(EntryId id) override;
  void SetEntryTitle(EntryId id, const std::u16string& title) override;
  // How many Today tabs currently carry a custom name. Test-only: a handle
  // is never recycled, so an entry left behind by a closed tab changes no
  // behaviour and a test cannot otherwise see the leak.
  size_t today_title_count_for_testing() const { return today_titles_.size(); }

  void SetTabTitle(int tab_index,
                   const GURL& expected_url,
                   const std::u16string& title) override;
  void ReturnToPinnedUrl(EntryId id) override;
  void MoveEntryToSection(EntryId id,
                          SidebarSection section,
                          int position) override;
  std::vector<SidebarFolder> folders() const override;
  void SetFolderCollapsed(FolderId id, bool collapsed) override;
  FolderId CreateFolderWithEntry(EntryId id,
                                 const std::u16string& name) override;
  bool CanCreateFolderWithEntry(EntryId id) const override;
  void MoveEntryToFolder(EntryId id,
                         std::optional<FolderId> folder_id) override;
  void SetFolderParent(FolderId id, std::optional<FolderId> parent_id) override;
  bool CanMoveFolderTo(FolderId id,
                       std::optional<FolderId> parent_id) const override;
  void SetFolderName(FolderId id, const std::u16string& name) override;
  void DeleteFolder(FolderId id) override;
  void SetArchiveTimeout(ArchiveTimeout timeout) override;
  ArchiveTimeout archive_timeout() const override;
  bool has_archive() const override;
  void RequestArchivedRows(int limit, ArchivedRowsCallback callback) override;
  void ReopenArchived(const GURL& url, base::Time archived_at) override;
  // Qualified: ArciumModel::Observer is also in scope through the base.
  void AddObserver(SidebarModel::Observer* observer) override;
  void RemoveObserver(SidebarModel::Observer* observer) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // ArciumModel::Observer:
  void OnArciumModelChanged() override;

  // SpaceSwitcher::Observer:
  void OnActiveSpaceChanged() override;

 private:
  // The space this window is showing. Without a switcher — the playground, a
  // window built with no sidebar, every fixture written before spaces — it is
  // the first space, which is what those callers have always meant.
  SpaceId active_space() const;
  // The entry's tab if it is live, in whichever window's strip it sits.
  tabs::TabInterface* BoundTabAnywhere(EntryId id) const;
  // The entry's tab if it is live and in this window's strip, else null.
  tabs::TabInterface* LiveTabForEntry(EntryId id) const;
  // True when an entry that *still exists* claims `tab`. A binding alone is
  // not enough: ArciumModel::ReplaceAll (which ModelStore::Load calls once
  // the window is interactive) removes entries without touching TabBinding,
  // and a tab left bound to a removed entry belongs in Today, not nowhere.
  // Every "does an entry own this tab" decision routes through here.
  bool IsClaimedByEntry(tabs::TabInterface* tab) const;
  // Selects `tab` in the strip that actually holds it and raises its window.
  void ActivateTabInItsOwnWindow(tabs::TabInterface* tab);
  SidebarRow RowForEntry(const TabEntry& entry) const;
  SidebarRow RowForTab(int index, tabs::TabInterface* tab) const;
  // Creates an entry of `kind` from the tab at `tab_index` and binds it.
  // Returns the new entry, or an invalid id when there was no such tab.
  EntryId AddEntryForTab(int tab_index, EntryKind kind);
  // The strip index of the `position`-th Today row — the tabs no entry claims,
  // in strip order — or -1 for past the last one. Today's order *is* the
  // strip's, so a drop into it is a strip move and this is how a place in the
  // list becomes a place in the strip.
  int TodayStripIndexForPosition(int position) const;
  // Moves the tab at `from` so it lands before the tab at `before`, or to the
  // end when `before` is -1.
  void MoveTabBeforeStripIndex(int from, int before);
  // The entry, if it exists and is one a folder may hold.
  const TabEntry* FolderableEntry(EntryId id) const;
  // Copies the live page title of every warm entry into the model, so a row
  // that later goes cold has something better than a URL to draw.
  void SyncEntryTitles();
  // Turns what the archive stores into what the list draws, then answers the
  // caller. A member rather than a free function so it can be bound through
  // this object's WeakPtr: a read still in flight when the window closes is
  // then dropped here, and the interface's promise that a callback may never
  // run holds whatever the caller bound it to.
  void DeliverArchivedRows(ArchivedRowsCallback callback,
                           ArchiveReadResult result);

  // Asks the profile for the stored icon of every entry with no tab behind
  // it. Called from the coalescing flush rather than from the const row
  // build, so a repaint never starts a lookup, and only after the model has
  // entries -- which is after ModelStore::Load, and so after first paint.
  void RequestColdFavicons();

  // Schedules FlushNotification() unless one is already pending.
  void NotifyChanged();
  void FlushNotification();

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<ArciumModel> arcium_model_;
  raw_ptr<TabBinding> binding_;
  raw_ptr<SpaceSwitcher> switcher_;
  raw_ptr<ArchiveService> archive_service_ = nullptr;
  base::ObserverList<SidebarModel::Observer> observers_;
  bool notification_pending_ = false;
  // Set while SyncEntryTitles() writes back into the model, so its own
  // mutations do not schedule a second notification for the same burst.
  bool suppress_model_notifications_ = false;
  // The entry awaiting the tab ActivateEntry() just asked for. Valid only
  // across that synchronous call.
  EntryId pending_bind_;
  // Custom names for Today tabs, which have no entry to hold one. Never
  // written to disk: the name dies with the tab. Keyed by handle rather than
  // strip index, because an index is only true at the instant it is read.
  //
  // An entry is erased on any removal from this strip, including a move to
  // another window -- unlike the binding, which survives that move. This
  // model draws one window, so a tab that has left it would leave a name
  // behind that nothing can reach or clear.
  std::map<tabs::TabHandle, std::u16string> today_titles_;
  // Cancels outstanding favicon lookups when this model goes away. Declared
  // before the cache so it outlives the callbacks the cache holds.
  base::CancelableTaskTracker favicon_tracker_;
  std::unique_ptr<ColdFaviconCache> cold_favicons_;
  base::WeakPtrFactory<SidebarTabModel> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_
