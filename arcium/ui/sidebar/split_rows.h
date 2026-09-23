// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SPLIT_ROWS_H_
#define ARCIUM_UI_SIDEBAR_SPLIT_ROWS_H_

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"

namespace arcium {

// Puts the two halves of each split next to each other in `rows`, in pane
// order, where the earlier of the two was, and joins them. A pair is two rows
// in one live split, or two pinned entries linked as one whether or not
// their tabs are open. Both halves are drawn in the earlier one's section and
// folder, and only the half with the focus -- the tab at `active_tab_index`
// -- stays current.
//
// A pair with a favourite in it is left where it is and unjoined: favourites
// are a grid of tiles, and a tile does not merge with a row.
//
// Here rather than in either model because both build rows and both must
// agree: the browser's model, and the playground's fake, which is how the
// pair is iterated on without a tab strip to make a split with.
void GroupSplitRows(std::vector<SidebarRow>& rows, int active_tab_index);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SPLIT_ROWS_H_
