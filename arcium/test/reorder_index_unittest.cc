// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/reorder_index.h"

#include <optional>

#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

// The rule itself, once, without a widget or a tab strip in the way. Every
// caller's own test below proves that caller reaches this; these prove what
// it answers.

TEST(ReorderIndexTest, ABoundaryBelowTheItemLosesTheSlotTheLiftFreed) {
  // Three items, the first one dragged into the gap before the third. The
  // boundary counts the list with the item still in it, so it is 2; lifting
  // item 0 out shifts items 1 and 2 up one, and the gap is now at 1.
  EXPECT_EQ(1, LiftThenInsertIndex(0, 2));
  EXPECT_EQ(2, LiftThenInsertIndex(1, 3));
}

TEST(ReorderIndexTest, ABoundaryAboveTheItemStands) {
  // Nothing above the item moves when it is lifted out, so the boundary the
  // drop was counted at is already the destination.
  EXPECT_EQ(0, LiftThenInsertIndex(2, 0));
  EXPECT_EQ(1, LiftThenInsertIndex(3, 1));
}

TEST(ReorderIndexTest, ABoundaryAtTheItemsOwnPlaceStands) {
  // A drop into the gap the item already occupies is a move to where it is.
  // Not `*from < to`, so no correction: correcting here would walk the item
  // one place up every time the user dropped it back where it started.
  EXPECT_EQ(2, LiftThenInsertIndex(2, 2));
}

TEST(ReorderIndexTest, AnItemFromAnotherListWasNeverCountedSoNothingShifts) {
  // An entry arriving from another section, or a tab in another window's
  // strip: it is not in the list the boundary was counted in, so the lift
  // frees no slot in that list and the boundary stands as read.
  EXPECT_EQ(2, LiftThenInsertIndex(std::nullopt, 2));
  EXPECT_EQ(0, LiftThenInsertIndex(std::nullopt, 0));
}

}  // namespace
}  // namespace arcium
