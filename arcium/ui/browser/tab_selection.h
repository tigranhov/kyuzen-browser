// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_TAB_SELECTION_H_
#define ARCIUM_UI_BROWSER_TAB_SELECTION_H_

#include <optional>

class TabStripModel;

namespace arcium {

// Which tab the strip activates when the active tab, or a block of tabs that
// holds it, is about to leave. `removed_index` and `removed_count` name that
// block while it is still in the strip. `chromium_choice` is Chromium's own
// pick, as an index into the strip after the removal, and the answer is in
// the same terms.
//
// Keeps Chromium's pick when it is in the space on screen, when the window
// has no sidebar, when nothing is left to select, and when the space has no
// other open tab. Otherwise answers with the space's nearest open tab,
// looking right first.
std::optional<int> NextSelectedIndexInSpace(TabStripModel* tab_strip_model,
                                            std::optional<int> chromium_choice,
                                            int removed_index,
                                            int removed_count);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_TAB_SELECTION_H_
