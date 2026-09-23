// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A pinned split is two pinned entries linked to each other, and the model
// keeps the pair whole through every move the sidebar can make.

#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/model_serializer.h"
#include "arcium/browser/model/tab_entry.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class SplitEntryModelTest : public testing::Test {
 protected:
  EntryId Pin(const char* host) {
    return model_.AddEntryForTesting(EntryKind::kPinned,
                                     GURL(std::string("https://") + host + "/"),
                                     std::u16string(host, host + strlen(host)));
  }

  // The pinned entries of the first space, in position order.
  std::vector<EntryId> PinnedOrder(SpaceId space = SpaceId()) {
    std::vector<EntryId> ids;
    for (const TabEntry* entry : model_.EntriesForKind(
             space.is_valid() ? space : model_.default_space_id(),
             EntryKind::kPinned)) {
      ids.push_back(entry->id);
    }
    return ids;
  }

  EntryId PartnerOf(EntryId id) { return model_.GetEntry(id)->split_partner; }

  ArciumModel model_;
};

TEST_F(SplitEntryModelTest, LinkingPutsThePartnerDirectlyAfterInTheSameFolder) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  const EntryId c = Pin("c.example");
  const FolderId folder = model_.AddFolderForTesting(u"Work");
  model_.SetEntryFolder(a, folder);

  ASSERT_TRUE(model_.LinkSplitEntries(a, c));

  EXPECT_EQ(c, PartnerOf(a));
  EXPECT_EQ(a, PartnerOf(c));
  EXPECT_EQ((std::vector<EntryId>{a, c, b}), PinnedOrder());
  EXPECT_EQ(folder, model_.GetEntry(c)->folder_id);
}

TEST_F(SplitEntryModelTest, AFavouriteIsNeverLinked) {
  const EntryId a = Pin("a.example");
  const EntryId fav = model_.AddEntryForTesting(
      EntryKind::kFavorite, GURL("https://fav.example/"), u"F");

  EXPECT_FALSE(model_.LinkSplitEntries(a, fav));
  EXPECT_FALSE(PartnerOf(a).is_valid());
  EXPECT_FALSE(PartnerOf(fav).is_valid());
}

TEST_F(SplitEntryModelTest, TwoSpacesAreNeverLinked) {
  const EntryId a = Pin("a.example");
  const SpaceId other = model_.AddSpace(u"Other");
  const EntryId b = model_.AddEntry(other, EntryKind::kPinned,
                                    GURL("https://b.example/"), u"B");

  EXPECT_FALSE(model_.LinkSplitEntries(a, b));
  EXPECT_FALSE(model_.LinkSplitEntries(a, a));
  EXPECT_FALSE(PartnerOf(a).is_valid());
}

TEST_F(SplitEntryModelTest, LinkingAgainLeavesTheOldPartnerAlone) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  const EntryId c = Pin("c.example");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));

  ASSERT_TRUE(model_.LinkSplitEntries(a, c));

  EXPECT_EQ(c, PartnerOf(a));
  EXPECT_FALSE(PartnerOf(b).is_valid());
}

TEST_F(SplitEntryModelTest, UnlinkingClearsBothSides) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));

  model_.UnlinkSplitEntry(b);

  EXPECT_FALSE(PartnerOf(a).is_valid());
  EXPECT_FALSE(PartnerOf(b).is_valid());
}

TEST_F(SplitEntryModelTest, RemovingOneHalfLeavesTheOtherUnlinked) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));

  model_.RemoveEntry(a);

  ASSERT_TRUE(model_.GetEntry(b));
  EXPECT_FALSE(PartnerOf(b).is_valid());
}

TEST_F(SplitEntryModelTest, ReorderingOneHalfMovesThePair) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  const EntryId c = Pin("c.example");
  const EntryId d = Pin("d.example");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));

  model_.ReorderEntry(b, 3);

  EXPECT_EQ((std::vector<EntryId>{c, d, a, b}), PinnedOrder());

  model_.ReorderEntry(a, 0);

  EXPECT_EQ((std::vector<EntryId>{a, b, c, d}), PinnedOrder());
}

TEST_F(SplitEntryModelTest, AHalfMadeAFavouriteLeavesThePair) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));

  model_.SetEntryKind(b, EntryKind::kFavorite);

  EXPECT_FALSE(PartnerOf(a).is_valid());
  EXPECT_FALSE(PartnerOf(b).is_valid());
}

TEST_F(SplitEntryModelTest, APairGoesIntoAFolderTogether) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  const FolderId folder = model_.AddFolderForTesting(u"Work");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));

  model_.SetEntryFolder(b, folder);

  EXPECT_EQ(folder, model_.GetEntry(a)->folder_id);
  EXPECT_EQ(folder, model_.GetEntry(b)->folder_id);
}

TEST_F(SplitEntryModelTest, APairMovesToAnotherSpaceTogetherAndInOrder) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  const SpaceId other = model_.AddSpace(u"Other");
  const EntryId there = model_.AddEntry(other, EntryKind::kPinned,
                                        GURL("https://t.example/"), u"T");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));

  model_.MoveEntryToSpace(b, other);

  EXPECT_EQ((std::vector<EntryId>{there, a, b}), PinnedOrder(other));
  EXPECT_TRUE(PinnedOrder().empty());
  EXPECT_EQ(b, PartnerOf(a));
}

TEST_F(SplitEntryModelTest, ALinkSurvivesTheFile) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(model_), &restored));

  EXPECT_EQ(b, restored.GetEntry(a)->split_partner);
  EXPECT_EQ(a, restored.GetEntry(b)->split_partner);
}

TEST_F(SplitEntryModelTest, AOneSidedLinkIsDroppedNotRefused) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));
  base::DictValue dict = SerializeModel(model_);
  for (base::Value& item : *dict.FindList("entries")) {
    if (*item.GetDict().FindString("id") == b.value()) {
      item.GetDict().Remove("split_partner");
    }
  }

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));

  ASSERT_TRUE(restored.GetEntry(a));
  EXPECT_FALSE(restored.GetEntry(a)->split_partner.is_valid());
}

TEST_F(SplitEntryModelTest, ALinkAcrossTwoKindsIsDroppedNotRefused) {
  const EntryId a = Pin("a.example");
  const EntryId b = Pin("b.example");
  ASSERT_TRUE(model_.LinkSplitEntries(a, b));
  base::DictValue dict = SerializeModel(model_);
  for (base::Value& item : *dict.FindList("entries")) {
    if (*item.GetDict().FindString("id") == b.value()) {
      item.GetDict().Set("kind", "favorite");
    }
  }

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));

  EXPECT_FALSE(restored.GetEntry(a)->split_partner.is_valid());
  EXPECT_FALSE(restored.GetEntry(b)->split_partner.is_valid());
}

}  // namespace
}  // namespace arcium
