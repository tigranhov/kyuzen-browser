// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SPLIT_ROWS_H_
#define ARCIUM_UI_SIDEBAR_SPLIT_ROWS_H_

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"

namespace arcium {

// Says which of `rows` are drawn joined, in place. Two rows are joined when
// they are the two halves of one split and sit next to each other in the same
// list: the same section, and the same folder, so a collapsed folder hides
// both or neither.
//
// Favourites are never joined. They are a grid rather than a list, so a row's
// neighbours are above and beside it and a bar down one edge would name the
// wrong pair; a favourite in a split carries the two-pane mark instead.
//
// Here rather than in either model because both build rows and both must
// agree: the browser's model, and the playground's fake, which is how the
// bracket is iterated on without a tab strip to make a split with.
void MarkSplitNeighbours(std::vector<SidebarRow>& rows);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SPLIT_ROWS_H_
