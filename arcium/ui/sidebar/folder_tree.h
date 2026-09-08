// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_FOLDER_TREE_H_
#define ARCIUM_UI_SIDEBAR_FOLDER_TREE_H_

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/model/entry_id.h"
#include "arcium/ui/sidebar/sidebar_model.h"

namespace arcium {

// Everything the flattening reads from a folder, whatever struct its owner
// keeps folders in. The browser model and the playground's fake hold
// different structs, and the walk is the same either way.
struct FolderInput {
  FolderId id;
  std::optional<FolderId> parent_id;
  int position = 0;
  std::u16string name;
  bool collapsed = false;
  // Entries directly inside this folder. The subtree total the sidebar draws
  // is rolled up from these here, so no caller has to walk the tree twice.
  int direct_entry_count = 0;
};

// `input` turned into the list the sidebar draws: pre-order -- a parent
// immediately before its descendants, siblings in `position` order -- with
// `depth` filled in and `entry_count` rolled up over each subtree.
//
// A folder whose parent is not in `input`, and a folder caught in a cycle,
// come back as roots rather than not at all. Dropping either would take its
// rows with it: a row names a folder id, and a row whose folder is not drawn
// is not drawn either, so a folder lost here is tabs lost from the sidebar.
std::vector<SidebarFolder> BuildSidebarFolders(std::vector<FolderInput> input);

// Whether `id` could become a child of `parent_id` -- or of the top level,
// for std::nullopt -- in the tree `folders` describes. Refuses an unknown id,
// the folder itself, any of its own descendants, and any move that would
// carry the moved subtree past kMaxFolderDepth.
//
// Answers from the flattened list, which is all a drop target or a test
// double can see: pre-order plus depth is enough, because a folder's subtree
// is exactly the run after it with a greater depth. SidebarTabModel does not
// use this -- it asks ArciumModel, which is the authority and enforces the
// same rule on itself. Two implementations on purpose: if they ever disagree,
// a test says so rather than a user finding out.
bool CanMoveFolderInTree(const std::vector<SidebarFolder>& folders,
                         FolderId id,
                         std::optional<FolderId> parent_id);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_FOLDER_TREE_H_
