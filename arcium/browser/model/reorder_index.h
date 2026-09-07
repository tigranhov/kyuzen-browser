// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_REORDER_INDEX_H_
#define ARCIUM_BROWSER_MODEL_REORDER_INDEX_H_

#include <optional>

namespace arcium {

// The destination index a lift-then-insert reorder needs, given a drop
// boundary that was counted with the moving item still in the list.
//
// Every reorder Arcium performs is lift-then-insert: ArciumModel::ReorderEntry
// erases the entry and re-inserts it, and TabStripModel::MoveWebContentsAt
// does the same to the strip. Every drop boundary, meanwhile, is read off a
// list the dragged row is still drawn in. Lifting the item out shifts
// everything below it up one, so a boundary that sat below the item overshoots
// by a slot — which is every downward drag, the commonest gesture there is.
//
// `from` is the item's own position in the same list `to` was counted in, or
// nullopt when it is not in that list at all: an entry arriving from another
// section, or a tab that lives in another window's strip. Such an item was
// never counted, so the lift frees no slot here and the boundary stands.
//
// One function rather than one correction per caller, for the reason
// IsClaimedByEntry is one function: four call sites decided this rule
// independently — two Views drop paths, the favourites grid, and the entry-
// to-Today move — and two of them had no test at all, because the coverage of
// the one that did was assumed to carry across. It did not.
int LiftThenInsertIndex(std::optional<int> from, int to);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_REORDER_INDEX_H_
