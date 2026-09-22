// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/split_rows.h"

namespace arcium {

void MarkSplitNeighbours(std::vector<SidebarRow>& rows) {
  for (size_t i = 1; i < rows.size(); ++i) {
    SidebarRow& above = rows[i - 1];
    SidebarRow& below = rows[i];
    if (!above.split.has_value() || above.split != below.split) {
      continue;
    }
    if (above.section != below.section ||
        above.section == SidebarSection::kFavorites ||
        above.folder_id != below.folder_id) {
      continue;
    }
    above.split_joins_next = true;
    below.split_joins_previous = true;
  }
}

}  // namespace arcium
