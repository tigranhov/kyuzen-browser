// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SPACE_SWITCHER_H_
#define ARCIUM_UI_BROWSER_SPACE_SWITCHER_H_

#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class TabStripModel;

namespace arcium {

class TabBinding;

// One per window. Holds which space the window is showing, answers whether a
// tab belongs to it, and performs switches.
//
// Deliberately not a member of BrowserSidebarController's model: the two
// patches of this stage are handed a Browser or a TabStripModel and nothing
// else, and a per-strip registry is reachable from both without dragging
// BrowserView into a unit test.
class SpaceSwitcher : public TabStripModelObserver,
                      public ArciumModel::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnActiveSpaceChanged() = 0;
  };

  SpaceSwitcher(TabStripModel* tab_strip_model,
                ArciumModel* model,
                TabBinding* binding);
  SpaceSwitcher(const SpaceSwitcher&) = delete;
  SpaceSwitcher& operator=(const SpaceSwitcher&) = delete;
  ~SpaceSwitcher() override;

  // The switcher of the window `tab_strip_model` belongs to, or null in a
  // window built without a sidebar (--arcium-no-sidebar, and every browser
  // test that does not want one).
  static SpaceSwitcher* FromTabStripModel(const TabStripModel* tab_strip_model);

  SpaceId active_space() const { return active_space_; }
  // Records the current space's active tab, moves to `id`, and lands on that
  // space's last active tab — or its first open tab, or a new blank one.
  void SwitchTo(SpaceId id);
  SpaceId SpaceOfTabAt(int index) const;
  bool IsInActiveSpace(int index) const;
  // Open tabs of `space`, in the order the sidebar draws them: favourites,
  // then pinned entries by position, then the tabs no entry claims in strip
  // order. Cold entries have no tab and are not here.
  std::vector<int> OpenTabsInSidebarOrder(SpaceId space) const;
  // A blank foreground tab in the active space. The strip index it landed at.
  int OpenBlankTab();
  void MoveTabToSpace(int index, SpaceId space);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // ArciumModel::Observer:
  void OnArciumModelChanged() override;

 private:
  void TagInsertedTabs(const TabStripModelChange::Insert& insert);
  void RecordActiveTab();
  void NotifyActiveSpaceChanged();
  // Posted from OnArciumModelChanged rather than run inline: see the comment
  // there. Re-checks that the active space is still gone before switching,
  // because the model can change again before this task runs.
  void ApplyFallbackSwitch();

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<ArciumModel> model_;
  raw_ptr<TabBinding> binding_;
  SpaceId active_space_;
  // Set while SwitchTo activates a tab, so the activation it causes is not
  // read back as the user choosing a foreign tab.
  bool switching_ = false;
  base::ObserverList<Observer> observers_;
  // Last: anything posted through this must run after every other member is
  // already constructed.
  base::WeakPtrFactory<SpaceSwitcher> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SPACE_SWITCHER_H_
