// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_
#define ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_

#include <optional>

#include "arcium/browser/model/entry_id.h"
#include "base/memory/raw_ptr.h"
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
class SplitController {
 public:
  // `switcher` is null in the playground, in a window built without a sidebar
  // and in every fixture written before spaces -- all of which mean one
  // space, so only the rules that do not mention spaces apply. Both arguments
  // must outlive this.
  // `model` is the sidebar's own model, null in a window without one; it is
  // consulted only to open a cold entry.
  SplitController(TabStripModel* tab_strip_model,
                  SpaceSwitcher* switcher,
                  SidebarTabModel* model);
  SplitController(const SplitController&) = delete;
  SplitController& operator=(const SplitController&) = delete;
  ~SplitController();

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

  // Ends the split the tab at `index` is in, if any. Called before a tab
  // leaves its space: a split whose halves are in two spaces must not exist
  // even for one turn of the loop.
  void EndSplitFor(int index);

  bool ActiveIsSplit() const;
  std::optional<split_tabs::SplitTabId> SplitOfActive() const;

 private:
  // Swaps the split's halves when `tab` is not on the side `right` says it
  // should be. Which half a tab is in is its strip index: the lower of the
  // two is the pane on the left.
  void PutOnSide(const split_tabs::SplitTabId& id,
                 tabs::TabHandle tab,
                 bool right);

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<SpaceSwitcher> switcher_;
  raw_ptr<SidebarTabModel> model_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_
