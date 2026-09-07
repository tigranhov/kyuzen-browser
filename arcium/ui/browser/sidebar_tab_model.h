// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_
#define ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_

#include <string>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class TabStripModel;

namespace tabs {
class TabInterface;
}

namespace arcium {

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
                        public ArciumModel::Observer {
 public:
  // `arcium_model` and `binding` are shared by every window on the profile
  // and must outlive this object.
  SidebarTabModel(TabStripModel* tab_strip_model,
                  ArciumModel* arcium_model,
                  TabBinding* binding);
  SidebarTabModel(const SidebarTabModel&) = delete;
  SidebarTabModel& operator=(const SidebarTabModel&) = delete;
  ~SidebarTabModel() override;

  // SidebarModel:
  std::vector<SidebarRow> rows() const override;
  void ActivateTab(int tab_index) override;
  void CloseTab(int tab_index) override;
  void MoveTab(int from_index, int to_index) override;
  void NewTab() override;
  void ClearToday() override;
  void AddToFavorites(int tab_index) override;
  void PinTab(int tab_index) override;
  void UnpinEntry(EntryId id) override;
  void ActivateEntry(EntryId id) override;
  void CloseEntryTab(EntryId id) override;
  void SetEntryTitle(EntryId id, const std::u16string& title) override;
  void ReturnToPinnedUrl(EntryId id) override;
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

 private:
  // The entry's tab if it is live, in whichever window's strip it sits.
  tabs::TabInterface* BoundTabAnywhere(EntryId id) const;
  // The entry's tab if it is live and in this window's strip, else null.
  tabs::TabInterface* LiveTabForEntry(EntryId id) const;
  // Selects `tab` in the strip that actually holds it and raises its window.
  void ActivateTabInItsOwnWindow(tabs::TabInterface* tab);
  SidebarRow RowForEntry(const TabEntry& entry) const;
  SidebarRow RowForTab(int index, tabs::TabInterface* tab) const;
  // Creates an entry of `kind` from the tab at `tab_index` and binds it.
  void AddEntryForTab(int tab_index, EntryKind kind);
  // Copies the live page title of every warm entry into the model, so a row
  // that later goes cold has something better than a URL to draw.
  void SyncEntryTitles();

  // Schedules FlushNotification() unless one is already pending.
  void NotifyChanged();
  void FlushNotification();

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<ArciumModel> arcium_model_;
  raw_ptr<TabBinding> binding_;
  base::ObserverList<SidebarModel::Observer> observers_;
  bool notification_pending_ = false;
  // Set while SyncEntryTitles() writes back into the model, so its own
  // mutations do not schedule a second notification for the same burst.
  bool suppress_model_notifications_ = false;
  // The entry awaiting the tab ActivateEntry() just asked for. Valid only
  // across that synchronous call.
  EntryId pending_bind_;
  base::WeakPtrFactory<SidebarTabModel> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_
