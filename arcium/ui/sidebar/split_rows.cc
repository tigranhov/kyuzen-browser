// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/split_rows.h"

#include <utility>

namespace arcium {

namespace {

bool CanJoin(const SidebarRow& a, const SidebarRow& b) {
  return a.section != SidebarSection::kFavorites &&
         b.section != SidebarSection::kFavorites;
}

}  // namespace

void GroupSplitRows(std::vector<SidebarRow>& rows, int active_tab_index) {
  const size_t n = rows.size();
  std::vector<int> partner(n, -1);
  // A live split first: when a linked entry is sharing the screen with some
  // other tab, the screen is what the row should say.
  for (size_t i = 0; i < n; ++i) {
    if (!rows[i].split.has_value() || partner[i] >= 0) {
      continue;
    }
    for (size_t j = i + 1; j < n; ++j) {
      if (rows[j].split == rows[i].split && CanJoin(rows[i], rows[j])) {
        partner[i] = static_cast<int>(j);
        partner[j] = static_cast<int>(i);
        break;
      }
    }
  }
  for (size_t i = 0; i < n; ++i) {
    if (!rows[i].split_partner.is_valid() || partner[i] >= 0) {
      continue;
    }
    for (size_t j = i + 1; j < n; ++j) {
      if (partner[j] < 0 && rows[j].entry_id == rows[i].split_partner &&
          rows[j].split_partner == rows[i].entry_id) {
        partner[i] = static_cast<int>(j);
        partner[j] = static_cast<int>(i);
        break;
      }
    }
  }

  std::vector<SidebarRow> grouped;
  grouped.reserve(n);
  std::vector<bool> placed(n, false);
  for (size_t i = 0; i < n; ++i) {
    if (placed[i]) {
      continue;
    }
    placed[i] = true;
    if (partner[i] < 0) {
      grouped.push_back(std::move(rows[i]));
      continue;
    }
    // `i` is the earlier of the two, since a pair is placed when its first
    // half is reached.
    const size_t j = static_cast<size_t>(partner[i]);
    placed[j] = true;
    const SidebarSection section = rows[i].section;
    const std::optional<FolderId> folder = rows[i].folder_id;
    const bool live =
        rows[i].split.has_value() && rows[i].split == rows[j].split;
    // The lower strip index is the left pane. A cold pair has no panes and
    // keeps the order its entries have.
    const bool swap = live && rows[j].tab_index < rows[i].tab_index;
    SidebarRow left = std::move(rows[swap ? j : i]);
    SidebarRow right = std::move(rows[swap ? i : j]);
    for (SidebarRow* half : {&left, &right}) {
      half->drawn_section = section;
      half->folder_id = folder;
      if (live && half->is_active) {
        half->is_active = half->tab_index == active_tab_index;
      }
    }
    left.split_joins_next = true;
    right.split_joins_previous = true;
    grouped.push_back(std::move(left));
    grouped.push_back(std::move(right));
  }
  rows = std::move(grouped);
}

}  // namespace arcium
