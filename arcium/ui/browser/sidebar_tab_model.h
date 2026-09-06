// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_
#define ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class TabStripModel;

namespace arcium {

// Adapts a window's TabStripModel to the SidebarModel interface. Rows are
// derived on demand from tabs::TabData, so nothing is cached per tab.
// Strip callbacks arrive in bursts (insert, then title, favicon and loading
// updates for the same tab); they are coalesced into one observer
// notification per run-loop turn, posted as a task. No timers.
class SidebarTabModel : public SidebarModel, public TabStripModelObserver {
 public:
  explicit SidebarTabModel(TabStripModel* tab_strip_model);
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
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

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

 private:
  // Schedules FlushNotification() unless one is already pending.
  void NotifyChanged();
  void FlushNotification();

  raw_ptr<TabStripModel> tab_strip_model_;
  base::ObserverList<Observer> observers_;
  bool notification_pending_ = false;
  base::WeakPtrFactory<SidebarTabModel> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_
