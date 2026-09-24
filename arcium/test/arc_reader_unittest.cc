// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/arc_reader.h"

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/import/import_plan.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

// A synthetic file with made-up names; never the owner's own.
ImportPlan ReadFixturePlan() {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string text;
  CHECK(base::ReadFileToString(
      root.AppendASCII("arcium/test/data/import/arc-sidebar.json"), &text));
  std::optional<ImportPlan> plan = ReadArcSidebar(text);
  CHECK(plan);
  return *plan;
}

// Wraps a main sidebar section in the file's outer shape.
std::string Sidebar(const std::string& section) {
  return R"({"sidebar": {"containers": [{"global": {}}, )" + section + "]}}";
}

const ImportSpace* SpaceNamed(const ImportPlan& plan, const std::string& name) {
  for (const ImportSpace& space : plan.spaces) {
    if (space.name == name) {
      return &space;
    }
  }
  return nullptr;
}

std::vector<std::string> Urls(const ImportPlan& plan,
                              ImportEntryKind kind,
                              const std::string& space_key) {
  std::vector<std::string> urls;
  for (const ImportEntry& entry : plan.entries) {
    if (entry.kind == kind && entry.space_key == space_key) {
      urls.push_back(entry.url.spec());
    }
  }
  return urls;
}

const ImportEntry* EntryAt(const ImportPlan& plan, const std::string& url) {
  for (const ImportEntry& entry : plan.entries) {
    if (entry.url.spec() == url) {
      return &entry;
    }
  }
  return nullptr;
}

// Arc's own named icons are not emoji, so that space draws a letter.
TEST(ArcReaderTest, SpacesKeepTheirOrderNamesAndIcons) {
  const ImportPlan plan = ReadFixturePlan();
  EXPECT_EQ(ImportSourceKind::kArc, plan.source);
  ASSERT_EQ(2u, plan.spaces.size());
  EXPECT_EQ("Personal", plan.spaces[0].name);
  EXPECT_EQ("\xF0\x9F\x8F\xA1", plan.spaces[0].icon);
  EXPECT_EQ("Work", plan.spaces[1].name);
  EXPECT_EQ("", plan.spaces[1].icon);
}

TEST(ArcReaderTest, AnotherArcProfileBecomesSeparateLogins) {
  const ImportPlan plan = ReadFixturePlan();
  ASSERT_EQ(1u, plan.profiles.size());
  EXPECT_EQ("Profile 1", plan.profiles[0].name);
  EXPECT_EQ(plan.profiles[0].key, SpaceNamed(plan, "Work")->profile_key);
  EXPECT_EQ("", SpaceNamed(plan, "Personal")->profile_key);
}

// Today's tabs and the note stay behind; a split view's tabs arrive as
// ordinary pinned tabs, where the split stood.
TEST(ArcReaderTest, PinnedTabsArriveInOrderAndNothingElse) {
  const ImportPlan plan = ReadFixturePlan();
  EXPECT_EQ(
      (std::vector<std::string>{
          "https://example.com/articles/one",
          "https://example.com/articles/deep", "https://docs.example.com/",
          "https://left.example.com/", "https://right.example.com/"}),
      Urls(plan, ImportEntryKind::kPinned, SpaceNamed(plan, "Personal")->key));
  // Work lists its Today section first; the marker, not the position, finds
  // the pinned one.
  EXPECT_EQ(
      (std::vector<std::string>{"https://work.example.com/board"}),
      Urls(plan, ImportEntryKind::kPinned, SpaceNamed(plan, "Work")->key));
  EXPECT_FALSE(EntryAt(plan, "https://news.example.com/today"));
}

TEST(ArcReaderTest, ARenamedTabKeepsItsName) {
  const ImportPlan plan = ReadFixturePlan();
  EXPECT_EQ("Team docs", EntryAt(plan, "https://docs.example.com/")->title);
  EXPECT_TRUE(EntryAt(plan, "https://docs.example.com/")->renamed);
  EXPECT_EQ("Article One",
            EntryAt(plan, "https://example.com/articles/one")->title);
  EXPECT_FALSE(EntryAt(plan, "https://example.com/articles/one")->renamed);
}

TEST(ArcReaderTest, FoldersNestAndHoldTheirTabs) {
  const ImportPlan plan = ReadFixturePlan();
  ASSERT_EQ(2u, plan.folders.size());
  const ImportFolder& reading = plan.folders[0];
  const ImportFolder& deep = plan.folders[1];
  EXPECT_EQ("Reading", reading.name);
  EXPECT_EQ("", reading.parent_key);
  EXPECT_EQ(SpaceNamed(plan, "Personal")->key, reading.space_key);
  EXPECT_EQ("Deep dive", deep.name);
  EXPECT_EQ(reading.key, deep.parent_key);
  EXPECT_EQ(reading.key,
            EntryAt(plan, "https://example.com/articles/one")->folder_key);
  EXPECT_EQ(deep.key,
            EntryAt(plan, "https://example.com/articles/deep")->folder_key);
  EXPECT_EQ("", EntryAt(plan, "https://left.example.com/")->folder_key);
}

// Arc keeps favourites per profile; every space on that profile shows them.
TEST(ArcReaderTest, FavouritesGoToTheSpacesOnTheirProfile) {
  const ImportPlan plan = ReadFixturePlan();
  EXPECT_EQ((std::vector<std::string>{"https://mail.example.com/"}),
            Urls(plan, ImportEntryKind::kFavorite,
                 SpaceNamed(plan, "Personal")->key));
  EXPECT_EQ(
      (std::vector<std::string>{"https://tracker.example.com/"}),
      Urls(plan, ImportEntryKind::kFavorite, SpaceNamed(plan, "Work")->key));
}

TEST(ArcReaderTest, CountsAreWhatTheSourceShowed) {
  const ImportPlan::Counts counts = ReadFixturePlan().counts();
  EXPECT_EQ(2u, counts.spaces);
  EXPECT_EQ(6u, counts.pinned);
  EXPECT_EQ(2u, counts.favorites);
  EXPECT_EQ(2u, counts.folders);
}

// A Little Arc window has a sidebar section of its own, listed before the
// main one; it is not the sidebar anyone set up.
TEST(ArcReaderTest, ALittleArcWindowIsNotTheSidebar) {
  const std::optional<ImportPlan> plan = ReadArcSidebar(
      R"({"sidebar": {"containers": [
          {"littleBrowser": {"_0": "L"}},
          {"spaces": ["x", {"id": "x", "title": "Little"}], "items": []},
          {"global": {}},
          {"spaces": ["m", {"id": "m", "title": "Main"}], "items": []}]}})");
  ASSERT_TRUE(plan);
  ASSERT_EQ(1u, plan->spaces.size());
  EXPECT_EQ("Main", plan->spaces[0].name);
}

// Older files give the emoji only as its code point.
TEST(ArcReaderTest, AnEmojiSavedAsANumberStillShows) {
  const std::optional<ImportPlan> plan = ReadArcSidebar(Sidebar(
      R"({"spaces": [{"id": "s", "title": "S",
                      "customInfo": {"iconType": {"emoji": 127970}}}]})"));
  ASSERT_TRUE(plan);
  ASSERT_EQ(1u, plan->spaces.size());
  EXPECT_EQ("\xF0\x9F\x8F\xA2", plan->spaces[0].icon);
}

// The newer marker list is read when the older one is missing.
TEST(ArcReaderTest, TheNewerMarkersFindThePinnedSectionToo) {
  const std::optional<ImportPlan> plan = ReadArcSidebar(Sidebar(
      R"({"spaces": [{"id": "s", "title": "S", "newContainerIDs": [
              {"unpinned": {}}, "u", {"pinned": {}}, "p"]}],
          "items": [
            {"id": "p", "childrenIds": ["t"], "data": {"itemContainer": {}}},
            {"id": "u", "childrenIds": ["n"], "data": {"itemContainer": {}}},
            {"id": "t", "data": {"tab": {"savedURL": "https://p.example/"}}},
            {"id": "n", "data": {"tab": {"savedURL": "https://n.example/"}}}
          ]})"));
  ASSERT_TRUE(plan);
  EXPECT_EQ((std::vector<std::string>{"https://p.example/"}),
            Urls(*plan, ImportEntryKind::kPinned, "s"));
}

// A damaged file where a folder holds itself must not hang the reader.
TEST(ArcReaderTest, AFolderThatHoldsItselfIsTakenOnce) {
  const std::optional<ImportPlan> plan = ReadArcSidebar(Sidebar(
      R"({"spaces": [{"id": "s", "title": "S",
                      "containerIDs": ["pinned", "p"]}],
          "items": [
            {"id": "p", "childrenIds": ["f"], "data": {"itemContainer": {}}},
            {"id": "f", "title": "F", "childrenIds": ["f", "t"],
             "data": {"list": {}}},
            {"id": "t", "data": {"tab": {"savedURL": "https://t.example/"}}}
          ]})"));
  ASSERT_TRUE(plan);
  ASSERT_EQ(1u, plan->folders.size());
  ASSERT_EQ(1u, plan->entries.size());
  EXPECT_EQ(plan->folders[0].key, plan->entries[0].folder_key);
}

// Arc syncs its sidebar, so a profile can carry a favourites row from each
// Mac. With nothing in this space's own row, it takes them all, once each.
TEST(ArcReaderTest, FavouriteRowsFromOtherMacsAreMerged) {
  const std::optional<ImportPlan> plan = ReadArcSidebar(Sidebar(
      R"({"spaces": [{"id": "s", "title": "S", "profile": {"custom": {"_0":
            {"directoryBasename": "Profile 2", "machineID": "HERE"}}}}],
          "items": [
            {"id": "r1", "childrenIds": ["a", "b"],
             "data": {"itemContainer": {}}},
            {"id": "r2", "childrenIds": ["c"], "data": {"itemContainer": {}}},
            {"id": "a", "data": {"tab": {"savedURL": "https://a.example/"}}},
            {"id": "b", "data": {"tab": {"savedURL": "https://b.example/"}}},
            {"id": "c", "data": {"tab": {"savedURL": "https://a.example/"}}}
          ],
          "topAppsContainerIDs": [
            {"custom": {"_0": {"directoryBasename": "Profile 2",
                               "machineID": "ELSEWHERE"}}}, "r1",
            {"custom": {"_0": {"directoryBasename": "Profile 2",
                               "machineID": "OTHER"}}}, "r2"]})"));
  ASSERT_TRUE(plan);
  EXPECT_EQ(
      (std::vector<std::string>{"https://a.example/", "https://b.example/"}),
      Urls(*plan, ImportEntryKind::kFavorite, "s"));
}

// A profile's folder name that is not one plain name is not trusted.
TEST(ArcReaderTest, AProfileNameThatIsAPathMeansTheDefaultProfile) {
  const std::optional<ImportPlan> plan = ReadArcSidebar(Sidebar(
      R"({"spaces": [{"id": "s", "title": "S", "profile": {"custom": {"_0":
            {"directoryBasename": "../Elsewhere"}}}}]})"));
  ASSERT_TRUE(plan);
  EXPECT_TRUE(plan->profiles.empty());
  EXPECT_EQ("", plan->spaces[0].profile_key);
}

// A tab whose address is gone is dropped.
TEST(ArcReaderTest, ATabWithNoAddressIsLeftOut) {
  const std::optional<ImportPlan> plan = ReadArcSidebar(Sidebar(
      R"({"spaces": [{"id": "s", "title": "S",
                      "containerIDs": ["pinned", "p"]}],
          "items": [
            {"id": "p", "childrenIds": ["t"], "data": {"itemContainer": {}}},
            {"id": "t", "data": {"tab": {"savedURL": ""}}}]})"));
  ASSERT_TRUE(plan);
  EXPECT_TRUE(plan->entries.empty());
}

TEST(ArcReaderTest, TextThatIsNotASidebarIsRefused) {
  EXPECT_FALSE(ReadArcSidebar("not json"));
  EXPECT_FALSE(ReadArcSidebar("[1]"));
  EXPECT_FALSE(ReadArcSidebar(R"({"tabs": []})"));
}

}  // namespace
}  // namespace arcium
