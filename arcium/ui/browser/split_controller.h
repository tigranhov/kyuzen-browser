// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_
#define ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_

#include <optional>

#include "arcium/browser/model/entry_id.h"
#include "base/memory/raw_ptr.h"
#include "components/split_tabs/split_tab_id.h"

class TabStripModel;

namespace arcium {

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
  SplitController(TabStripModel* tab_strip_model, SpaceSwitcher* switcher);
  SplitController(const SplitController&) = delete;
  SplitController& operator=(const SplitController&) = delete;
  ~SplitController();

  // Whether the tabs at these two indices may share the screen. False for a
  // bad index, for two spaces, for a loose page, and for a tab already in a
  // split.
  bool CanSplit(int index_a, int index_b) const;

  // Puts the tab at `index` beside the tab on screen. False when refused, and
  // then nothing happened.
  bool SplitWithActive(int index);

  // Ends the split the tab on screen is in. Both tabs stay open.
  void Unsplit();

  // Ends the split the tab at `index` is in, if any. Called before a tab
  // leaves its space: a split whose halves are in two spaces must not exist
  // even for one turn of the loop.
  void EndSplitFor(int index);

  bool ActiveIsSplit() const;
  std::optional<split_tabs::SplitTabId> SplitOfActive() const;

 private:
  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<SpaceSwitcher> switcher_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_
