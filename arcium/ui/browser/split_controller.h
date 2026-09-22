// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_
#define ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_

#include <optional>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/tab_interface.h"

class TabStripModel;

namespace arcium {

class SidebarTabModel;
class SpaceSwitcher;

// One window's split view (R5.1): the decisions Chromium's own split cannot
// make. Chromium owns the two panes, the divider and the collection inside
// the strip; this owns who may share a screen with whom, and ends a split
// that Arcium's own rules would otherwise break.
//
// Two panes, never more. SplitTabLayout has two values, SplitTabVisualData
// holds one ratio and MultiContentsView holds two contents views; three and
// four are deferred, with the reason in the stage's design.
class SplitController : public TabStripModelObserver,
                        public ArciumModel::Observer {
 public:
  // `switcher` is null in the playground, in a window built without a sidebar
  // and in every fixture written before spaces -- all of which mean one
  // space, so only the rules that do not mention spaces apply. Both arguments
  // must outlive this.
  // `model` is the sidebar's own model, null in a window without one; it is
  // consulted only to open a cold entry. `arcium_model` is where a split is
  // written down so it survives a relaunch, and is null in the fixtures that
  // do not care.
  SplitController(TabStripModel* tab_strip_model,
                  SpaceSwitcher* switcher,
                  SidebarTabModel* model,
                  ArciumModel* arcium_model = nullptr);
  SplitController(const SplitController&) = delete;
  SplitController& operator=(const SplitController&) = delete;
  ~SplitController() override;

  // Whether the tabs at these two indices may share the screen. False for a
  // bad index, for two spaces, for a loose page, and for a tab already in a
  // split.
  bool CanSplit(int index_a, int index_b) const;

  // Puts the tab at `index` beside the tab on screen. False when refused, and
  // then nothing happened.
  //
  // `on_right` is which side that tab should take, which only a drop knows:
  // it was dropped on one half of the page and belongs there. Absent leaves
  // the order Chromium chose, which is strip order.
  bool SplitWithActive(int index, std::optional<bool> on_right = std::nullopt);

  // The same for a sidebar entry, opening its page first when the entry is
  // cold. Needs the sidebar's model, which is what knows how to open one;
  // false when this window has none.
  bool SplitWithActive(EntryId id, std::optional<bool> on_right = std::nullopt);

  // Ends the split the tab on screen is in. Both tabs stay open.
  void Unsplit();

  // Splits the page on screen with the one the reader was on before it, or
  // ends the split when there is one. The keyboard's way in and out.
  void ToggleSplitWithPrevious();

  // Ends the split the tab at `index` is in, if any. Called before a tab
  // leaves its space: a split whose halves are in two spaces must not exist
  // even for one turn of the loop.
  void EndSplitFor(int index);

  bool ActiveIsSplit() const;
  std::optional<split_tabs::SplitTabId> SplitOfActive() const;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;

  // ArciumModel::Observer: the model file is read after the window is up, so
  // the record a launch has to act on usually arrives here rather than being
  // there when this was built.
  void OnArciumModelChanged() override;

 private:
  // Writes the split the window is showing into the space it belongs to, or
  // clears the record when there is none. Called whenever the strip reports
  // a split changing.
  void RecordSplitOfActiveSpace();
  // Asks for a re-forming pass on the next turn of the loop, at most one at
  // a time. Never synchronous: both callers arrive from inside a
  // notification -- a tab insertion, or a model change -- and this mutates
  // the strip, which the strip's own reentrancy guard refuses.
  void ScheduleReform();
  // Re-forms any recorded split whose two tabs are both in the strip and not
  // already sharing the screen. Walks the spaces, which is a handful, and
  // stops at the first question for each: most have no record, and one that
  // has been re-formed is refused by CanSplit ever after. Nothing is left
  // running between calls -- no timer, no list, no deadline for a record
  // whose second tab never arrives.
  void ReformRecordedSplits();
  // The tab carrying `key`, or null.
  tabs::TabInterface* TabWithKey(TabKey key) const;

  // Swaps the split's halves when `tab` is not on the side `right` says it
  // should be. Which half a tab is in is its strip index: the lower of the
  // two is the pane on the left.
  void PutOnSide(const split_tabs::SplitTabId& id,
                 tabs::TabHandle tab,
                 bool right);

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<SpaceSwitcher> switcher_;
  raw_ptr<SidebarTabModel> model_;
  raw_ptr<ArciumModel> arcium_model_;
  // True while this controller is forming a split itself, so the record it is
  // acting on is not immediately rewritten from the split it just made.
  bool reforming_ = false;
  bool reform_scheduled_ = false;

  // The tab that was on screen before the one that is. A handle, because the
  // tab it names can be closed or moved between one activation and the next,
  // and a handle reads as null rather than as somebody else.
  tabs::TabHandle previously_active_;

  base::WeakPtrFactory<SplitController> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_
