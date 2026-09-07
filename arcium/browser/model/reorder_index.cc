// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/reorder_index.h"

#include <optional>

namespace arcium {

int LiftThenInsertIndex(std::optional<int> from, int to) {
  // Strictly less than: a drop into the gap the item already occupies is a
  // move to where it already is, and correcting that would walk the item one
  // place up every time the user put it back where they found it.
  return from.has_value() && *from < to ? to - 1 : to;
}

}  // namespace arcium
