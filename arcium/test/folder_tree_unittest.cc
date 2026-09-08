// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/folder_tree.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "arcium/browser/model/folder.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

FolderInput Input(FolderId id,
                  std::optional<FolderId> parent,
                  int position,
                  int direct_entries = 0) {
  FolderInput input;
  input.id = id;
  input.parent_id = parent;
  input.position = position;
  input.direct_entry_count = direct_entries;
  return input;
}

std::vector<FolderId> IdsOf(const std::vector<SidebarFolder>& folders) {
  std::vector<FolderId> ids;
  for (const SidebarFolder& folder : folders) {
    ids.push_back(folder.id);
  }
  return ids;
}

int DepthOf(const std::vector<SidebarFolder>& folders, FolderId id) {
  for (const SidebarFolder& folder : folders) {
    if (folder.id == id) {
      return folder.depth;
    }
  }
  return -1;
}

TEST(FolderTreeTest, FoldersComeBackInPreOrderWithTheirDepth) {
  const FolderId first = FolderId::Generate();
  const FolderId first_a = FolderId::Generate();
  const FolderId first_a_deep = FolderId::Generate();
  const FolderId first_b = FolderId::Generate();
  const FolderId second = FolderId::Generate();

  // Deliberately not in tree order, and not in position order either: the
  // ordering is this function's job, not its caller's.
  std::vector<SidebarFolder> out = BuildSidebarFolders({
      Input(second, std::nullopt, 1),
      Input(first_b, first, 1),
      Input(first_a_deep, first_a, 0),
      Input(first, std::nullopt, 0),
      Input(first_a, first, 0),
  });

  EXPECT_EQ(
      (std::vector<FolderId>{first, first_a, first_a_deep, first_b, second}),
      IdsOf(out));
  ASSERT_EQ(5u, out.size());
  EXPECT_EQ(0, out[0].depth);
  EXPECT_EQ(1, out[1].depth);
  EXPECT_EQ(2, out[2].depth);
  EXPECT_EQ(1, out[3].depth);
  EXPECT_EQ(0, out[4].depth);
}

TEST(FolderTreeTest, ACountIncludesTheWholeSubtree) {
  const FolderId outer = FolderId::Generate();
  const FolderId inner = FolderId::Generate();
  const FolderId deepest = FolderId::Generate();

  std::vector<SidebarFolder> out = BuildSidebarFolders({
      Input(outer, std::nullopt, 0, /*direct_entries=*/1),
      Input(inner, outer, 0, /*direct_entries=*/2),
      Input(deepest, inner, 0, /*direct_entries=*/4),
  });

  ASSERT_EQ(3u, out.size());
  // A collapsed folder holding only subfolders would otherwise read "0"
  // while hiding everything under it, which is the one moment the count
  // matters most.
  EXPECT_EQ(7, out[0].entry_count);
  EXPECT_EQ(6, out[1].entry_count);
  EXPECT_EQ(4, out[2].entry_count);
}

TEST(FolderTreeTest, AFolderWhoseParentIsMissingIsDrawnAsARoot) {
  const FolderId root = FolderId::Generate();
  const FolderId orphan = FolderId::Generate();

  std::vector<SidebarFolder> out = BuildSidebarFolders({
      Input(root, std::nullopt, 0),
      Input(orphan, FolderId::Generate(), 1),
  });

  ASSERT_EQ(2u, out.size());
  // Both drawn. A folder that vanishes takes its rows with it, because a row
  // naming a folder nothing draws is not drawn either.
  EXPECT_EQ(0, out[1].depth);
  // Reported as a root as well as drawn as one: depth 0 and a parent must
  // never disagree.
  EXPECT_FALSE(out[1].parent_id.has_value());
}

TEST(FolderTreeTest, ACycleIsFlattenedRatherThanDroppedOrHung) {
  const FolderId a = FolderId::Generate();
  const FolderId b = FolderId::Generate();

  std::vector<SidebarFolder> out =
      BuildSidebarFolders({Input(a, b, 0), Input(b, a, 1)});

  // Neither is reachable from a root, so neither may be lost.
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(0, out[0].depth);
  EXPECT_EQ(0, out[1].depth);
}

TEST(FolderTreeTest, AFolderCannotMoveIntoItselfOrItsDescendant) {
  const FolderId outer = FolderId::Generate();
  const FolderId inner = FolderId::Generate();
  const FolderId other = FolderId::Generate();
  const std::vector<SidebarFolder> tree = BuildSidebarFolders({
      Input(outer, std::nullopt, 0),
      Input(inner, outer, 0),
      Input(other, std::nullopt, 1),
  });

  EXPECT_FALSE(CanMoveFolderInTree(tree, outer, outer));
  EXPECT_FALSE(CanMoveFolderInTree(tree, outer, inner));
  EXPECT_TRUE(CanMoveFolderInTree(tree, other, inner));
  EXPECT_TRUE(CanMoveFolderInTree(tree, inner, std::nullopt));
  EXPECT_FALSE(CanMoveFolderInTree(tree, FolderId::Generate(), std::nullopt));
}

TEST(FolderTreeTest, AMoveThatWouldBustTheDepthCapIsRefused) {
  // Built from kMaxFolderDepth rather than from a fixed shape. The first
  // version of this test hard-coded a three-level cap; the cap is five, so
  // the move it called illegal was legal and the test failed for a reason
  // that had nothing to do with the rule it was checking. A shape derived
  // from the constant cannot go stale that way.
  std::vector<FolderInput> input;
  std::vector<FolderId> host;
  for (int i = 0; i < kMaxFolderDepth - 1; ++i) {
    host.push_back(FolderId::Generate());
    input.push_back(Input(host.back(),
                          i == 0 ? std::optional<FolderId>()
                                 : std::optional<FolderId>(host[i - 1]),
                          0));
  }
  const FolderId top = FolderId::Generate();
  const FolderId mid = FolderId::Generate();
  input.push_back(Input(top, std::nullopt, 1));
  input.push_back(Input(mid, top, 0));
  const std::vector<SidebarFolder> tree = BuildSidebarFolders(std::move(input));

  // The host chain fills every level but the last, so its deepest folder sits
  // one short of the bottom.
  const FolderId deepest = host.back();
  ASSERT_EQ(kMaxFolderDepth - 2, DepthOf(tree, deepest));
  // `top` would land on the last level and carry `mid` one past it.
  EXPECT_FALSE(CanMoveFolderInTree(tree, top, deepest));
  // `mid` brings no height of its own, so it lands exactly on the last level.
  EXPECT_TRUE(CanMoveFolderInTree(tree, mid, deepest));
}

}  // namespace
}  // namespace arcium
