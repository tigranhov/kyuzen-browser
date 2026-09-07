// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"

#include <algorithm>
#include <vector>

#include "arcium/browser/model/tab_entry.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class CountingObserver : public ArciumModel::Observer {
 public:
  void OnArciumModelChanged() override { ++count; }
  int count = 0;
};

class ArciumModelTest : public testing::Test {
 protected:
  ArciumModel model_;
};

TEST_F(ArciumModelTest, StartsWithOneDefaultSpace) {
  ASSERT_EQ(1u, model_.spaces().size());
  EXPECT_EQ(model_.default_space_id(), model_.spaces()[0].id);
  EXPECT_EQ(ArchiveTimeout::kTwelveHours, model_.spaces()[0].archive_timeout);
}

TEST_F(ArciumModelTest, AddEntryReturnsAStableUniqueId) {
  const EntryId a =
      model_.AddEntry(EntryKind::kFavorite, GURL("https://a.example/"), u"A");
  const EntryId b =
      model_.AddEntry(EntryKind::kPinned, GURL("https://b.example/"), u"B");
  EXPECT_NE(a, b);
  ASSERT_TRUE(model_.GetEntry(a));
  EXPECT_EQ(EntryKind::kFavorite, model_.GetEntry(a)->kind);
  EXPECT_EQ(GURL("https://a.example/"), model_.GetEntry(a)->url);
  EXPECT_EQ(u"A", model_.GetEntry(a)->last_title);
  EXPECT_EQ(model_.default_space_id(), model_.GetEntry(a)->space_id);
}

TEST_F(ArciumModelTest, EntriesForKindComeBackInPositionOrder) {
  const EntryId first =
      model_.AddEntry(EntryKind::kPinned, GURL("https://1.example/"), u"1");
  const EntryId second =
      model_.AddEntry(EntryKind::kPinned, GURL("https://2.example/"), u"2");
  std::vector<const TabEntry*> pinned =
      model_.EntriesForKind(model_.default_space_id(), EntryKind::kPinned);
  ASSERT_EQ(2u, pinned.size());
  EXPECT_EQ(first, pinned[0]->id);
  EXPECT_EQ(second, pinned[1]->id);
}

TEST_F(ArciumModelTest, SetEntryKindMovesBetweenSections) {
  const EntryId id =
      model_.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  model_.SetEntryKind(id, EntryKind::kFavorite);
  EXPECT_EQ(EntryKind::kFavorite, model_.GetEntry(id)->kind);
  EXPECT_TRUE(
      model_.EntriesForKind(model_.default_space_id(), EntryKind::kPinned)
          .empty());
  EXPECT_EQ(
      1u, model_.EntriesForKind(model_.default_space_id(), EntryKind::kFavorite)
              .size());
}

TEST_F(ArciumModelTest, CustomTitleWinsOverLastTitle) {
  const EntryId id = model_.AddEntry(EntryKind::kPinned,
                                     GURL("https://a.example/"), u"Page title");
  EXPECT_EQ(u"Page title", model_.GetEntry(id)->DisplayTitle());
  model_.SetCustomTitle(id, u"My name");
  EXPECT_EQ(u"My name", model_.GetEntry(id)->DisplayTitle());
  // A later page title does not override a custom one.
  model_.SetLastTitle(id, u"New page title");
  EXPECT_EQ(u"My name", model_.GetEntry(id)->DisplayTitle());
  // Clearing the custom title restores the page title.
  model_.SetCustomTitle(id, u"");
  EXPECT_EQ(u"New page title", model_.GetEntry(id)->DisplayTitle());
}

TEST_F(ArciumModelTest, ReorderEntryMovesItWithinItsKind) {
  const EntryId a =
      model_.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  const EntryId b =
      model_.AddEntry(EntryKind::kPinned, GURL("https://b.example/"), u"B");
  const EntryId c =
      model_.AddEntry(EntryKind::kPinned, GURL("https://c.example/"), u"C");
  model_.ReorderEntry(c, 0);
  std::vector<const TabEntry*> pinned =
      model_.EntriesForKind(model_.default_space_id(), EntryKind::kPinned);
  ASSERT_EQ(3u, pinned.size());
  EXPECT_EQ(c, pinned[0]->id);
  EXPECT_EQ(a, pinned[1]->id);
  EXPECT_EQ(b, pinned[2]->id);
}

TEST_F(ArciumModelTest, RemoveEntryDropsIt) {
  const EntryId id =
      model_.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  model_.RemoveEntry(id);
  EXPECT_FALSE(model_.GetEntry(id));
}

TEST_F(ArciumModelTest, FoldersHoldPinnedEntries) {
  const FolderId folder = model_.AddFolder(u"Work");
  const EntryId id =
      model_.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  model_.SetEntryFolder(id, folder);
  EXPECT_EQ(folder, model_.GetEntry(id)->folder_id);

  model_.SetFolderCollapsed(folder, true);
  ASSERT_TRUE(model_.GetFolder(folder));
  EXPECT_TRUE(model_.GetFolder(folder)->collapsed);

  model_.SetFolderName(folder, u"Personal");
  EXPECT_EQ(u"Personal", model_.GetFolder(folder)->name);
}

TEST_F(ArciumModelTest, RemovingAFolderReturnsItsEntriesToTheTopLevel) {
  const FolderId folder = model_.AddFolder(u"Work");
  const EntryId id =
      model_.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  model_.SetEntryFolder(id, folder);
  model_.RemoveFolder(folder);
  ASSERT_TRUE(model_.GetEntry(id));
  EXPECT_FALSE(model_.GetEntry(id)->folder_id.has_value());
}

TEST_F(ArciumModelTest, ObserverFiresOnEveryMutation) {
  CountingObserver observer;
  model_.AddObserver(&observer);
  const EntryId id =
      model_.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  EXPECT_EQ(1, observer.count);
  model_.SetCustomTitle(id, u"X");
  EXPECT_EQ(2, observer.count);
  model_.RemoveEntry(id);
  EXPECT_EQ(3, observer.count);
  model_.RemoveObserver(&observer);
  model_.AddEntry(EntryKind::kPinned, GURL("https://b.example/"), u"B");
  EXPECT_EQ(3, observer.count);
}

TEST_F(ArciumModelTest, MutatingAnUnknownIdIsANoOpNotACrash) {
  const EntryId missing = EntryId::Generate();
  model_.SetCustomTitle(missing, u"X");
  model_.SetEntryKind(missing, EntryKind::kFavorite);
  model_.ReorderEntry(missing, 0);
  model_.RemoveEntry(missing);
  EXPECT_FALSE(model_.GetEntry(missing));
}

TEST_F(ArciumModelTest, FolderPositionsStayContiguousAcrossRemoveAndAdd) {
  const FolderId a = model_.AddFolder(u"A");
  const FolderId b = model_.AddFolder(u"B");
  const FolderId c = model_.AddFolder(u"C");
  model_.RemoveFolder(b);
  const FolderId d = model_.AddFolder(u"D");

  // Positions must be 0..n-1 with no duplicates, or Task 7's folder list
  // orders headers arbitrarily.
  std::vector<int> positions;
  for (const Folder& folder : model_.folders()) {
    positions.push_back(folder.position);
  }
  std::sort(positions.begin(), positions.end());
  EXPECT_EQ((std::vector<int>{0, 1, 2}), positions);
  EXPECT_NE(model_.GetFolder(c)->position, model_.GetFolder(d)->position);
  EXPECT_EQ(3u, model_.folders().size());
  EXPECT_TRUE(model_.GetFolder(a));
}

TEST_F(ArciumModelTest, ReplaceAllSwapsTheWholeModelAndNotifies) {
  CountingObserver observer;
  model_.AddEntry(EntryKind::kPinned, GURL("https://old.example/"), u"Old");
  model_.AddObserver(&observer);

  Space space;
  space.id = SpaceId::Generate();
  space.name = u"Loaded";
  TabEntry entry;
  entry.id = EntryId::Generate();
  entry.kind = EntryKind::kFavorite;
  entry.space_id = space.id;
  entry.url = GURL("https://new.example/");
  entry.last_title = u"New";

  model_.ReplaceAll({space}, {}, {entry});

  EXPECT_EQ(1, observer.count);
  ASSERT_EQ(1u, model_.entries().size());
  EXPECT_EQ(GURL("https://new.example/"), model_.entries()[0].url);
  EXPECT_EQ(space.id, model_.default_space_id());
  model_.RemoveObserver(&observer);
}

// The lookup two adapters were each doing by hand: a linear scan over spaces_
// with a hardcoded twelve-hour fallback beside it. GetSpace is the accessor
// GetEntry and GetFolder already had, and the fallback is now Space's own
// default rather than a literal repeated per caller.
TEST_F(ArciumModelTest, GetSpaceFindsASpaceAndRefusesAnUnknownId) {
  Space first;
  first.id = SpaceId::Generate();
  first.name = u"First";
  first.archive_timeout = ArchiveTimeout::kSevenDays;
  Space other;
  other.id = SpaceId::Generate();
  other.name = u"Other";
  other.archive_timeout = ArchiveTimeout::kNever;
  model_.ReplaceAll({first, other}, {}, {});

  ASSERT_TRUE(model_.GetSpace(first.id));
  EXPECT_EQ(ArchiveTimeout::kSevenDays,
            model_.GetSpace(first.id)->archive_timeout);
  ASSERT_TRUE(model_.GetSpace(other.id));
  EXPECT_EQ(ArchiveTimeout::kNever, model_.GetSpace(other.id)->archive_timeout);
  EXPECT_FALSE(model_.GetSpace(SpaceId::Generate()));
  EXPECT_FALSE(model_.GetSpace(SpaceId()));
}

TEST_F(ArciumModelTest, ReplaceAllWithNoSpacesStillLeavesOneUsableSpace) {
  model_.ReplaceAll({}, {}, {});
  EXPECT_EQ(1u, model_.spaces().size());
  EXPECT_TRUE(model_.default_space_id().is_valid());
}

TEST_F(ArciumModelTest, FromStringRejectsAnythingNotAWellFormedUuid) {
  EXPECT_FALSE(EntryId::FromString("").is_valid());
  EXPECT_FALSE(EntryId::FromString("not-a-uuid").is_valid());
  EXPECT_FALSE(EntryId::FromString("00000000-0000-4000-8000").is_valid());
  const EntryId generated = EntryId::Generate();
  const EntryId parsed = EntryId::FromString(generated.value());
  EXPECT_TRUE(parsed.is_valid());
  EXPECT_EQ(generated, parsed);
}

}  // namespace
}  // namespace arcium
