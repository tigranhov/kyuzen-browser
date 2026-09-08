// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/folder_tree.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "arcium/browser/model/folder.h"

namespace arcium {

std::vector<SidebarFolder> BuildSidebarFolders(std::vector<FolderInput> input) {
  std::set<FolderId> present;
  for (const FolderInput& folder : input) {
    present.insert(folder.id);
  }

  std::map<std::optional<FolderId>, std::vector<size_t>> children;
  for (size_t i = 0; i < input.size(); ++i) {
    std::optional<FolderId> parent = input[i].parent_id;
    // A parent that is not here makes this folder a root: the walk starts at
    // the roots and would never reach it otherwise.
    if (parent.has_value() && !present.count(*parent)) {
      parent.reset();
    }
    children[parent].push_back(i);
  }
  for (auto& [parent, siblings] : children) {
    std::stable_sort(siblings.begin(), siblings.end(),
                     [&input](size_t a, size_t b) {
                       return input[a].position < input[b].position;
                     });
  }

  // Indices in the order they are drawn, and the depth each is drawn at. An
  // explicit stack rather than recursion: this tree's shape came off disk,
  // and a depth that cannot be bounded by reading the code is not a depth to
  // spend real stack on. Pushed in reverse so siblings pop in position order.
  std::vector<size_t> order;
  std::vector<int> order_depth;
  std::vector<bool> emitted(input.size(), false);
  std::vector<std::pair<size_t, int>> stack;
  const auto roots = children.find(std::nullopt);
  if (roots != children.end()) {
    for (auto it = roots->second.rbegin(); it != roots->second.rend(); ++it) {
      stack.emplace_back(*it, 0);
    }
  }
  while (!stack.empty()) {
    const auto [index, depth] = stack.back();
    stack.pop_back();
    if (emitted[index]) {
      continue;
    }
    emitted[index] = true;
    order.push_back(index);
    order_depth.push_back(depth);
    const auto next = children.find(input[index].id);
    if (next == children.end()) {
      continue;
    }
    for (auto it = next->second.rbegin(); it != next->second.rend(); ++it) {
      stack.emplace_back(*it, depth + 1);
    }
  }
  // Whatever the walk could not reach -- folders in a cycle no repair pass
  // has run over -- is drawn flat rather than not at all.
  for (size_t i = 0; i < input.size(); ++i) {
    if (!emitted[i]) {
      emitted[i] = true;
      order.push_back(i);
      order_depth.push_back(0);
    }
  }

  std::map<FolderId, size_t> index_of;
  for (size_t i = 0; i < input.size(); ++i) {
    index_of[input[i].id] = i;
  }
  std::vector<int> totals;
  totals.reserve(input.size());
  for (const FolderInput& folder : input) {
    totals.push_back(folder.direct_entry_count);
  }
  // Pre-order means a folder always precedes its descendants, so one reverse
  // pass adds each subtree's total into its parent's.
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    const std::optional<FolderId>& parent = input[*it].parent_id;
    if (!parent.has_value()) {
      continue;
    }
    const auto found = index_of.find(*parent);
    if (found != index_of.end() && found->second != *it) {
      totals[found->second] += totals[*it];
    }
  }

  std::vector<SidebarFolder> result;
  result.reserve(order.size());
  for (size_t i = 0; i < order.size(); ++i) {
    const FolderInput& folder = input[order[i]];
    SidebarFolder out;
    out.id = folder.id;
    // Depth 0 means root, and the two must never disagree: a folder drawn
    // flat because its parent is missing is reported as having none.
    out.parent_id = order_depth[i] == 0 ? std::nullopt : folder.parent_id;
    out.depth = order_depth[i];
    out.name = folder.name;
    out.collapsed = folder.collapsed;
    out.entry_count = totals[order[i]];
    result.push_back(std::move(out));
  }
  return result;
}

bool CanMoveFolderInTree(const std::vector<SidebarFolder>& folders,
                         FolderId id,
                         std::optional<FolderId> parent_id) {
  size_t index = folders.size();
  for (size_t i = 0; i < folders.size(); ++i) {
    if (folders[i].id == id) {
      index = i;
      break;
    }
  }
  if (index == folders.size()) {
    return false;
  }
  // The moved folder's subtree is the run after it with a greater depth --
  // which is the whole reason this list is pre-ordered. Its height is how
  // much depth it carries wherever it lands.
  const int depth = folders[index].depth;
  int height = 0;
  size_t end = index + 1;
  while (end < folders.size() && folders[end].depth > depth) {
    height = std::max(height, folders[end].depth - depth);
    ++end;
  }

  int new_depth = 0;
  if (parent_id.has_value()) {
    if (*parent_id == id) {
      return false;
    }
    // Inside the run is exactly "is a descendant of the folder being moved".
    for (size_t i = index + 1; i < end; ++i) {
      if (folders[i].id == *parent_id) {
        return false;
      }
    }
    size_t parent = folders.size();
    for (size_t i = 0; i < folders.size(); ++i) {
      if (folders[i].id == *parent_id) {
        parent = i;
        break;
      }
    }
    if (parent == folders.size()) {
      return false;
    }
    new_depth = folders[parent].depth + 1;
  }
  return new_depth + height <= kMaxFolderDepth - 1;
}

}  // namespace arcium
