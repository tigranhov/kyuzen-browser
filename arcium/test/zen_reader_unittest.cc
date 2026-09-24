// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/zen_reader.h"

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/import/import_plan.h"
#include "arcium/browser/import/mozlz4.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

std::string ReadFixture(const char* name) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string text;
  CHECK(base::ReadFileToString(
      root.AppendASCII("arcium/test/data/import").AppendASCII(name), &text));
  return text;
}

// A synthetic file with made-up names; never the owner's own.
ImportPlan ReadFixturePlan() {
  std::optional<ImportPlan> plan = ReadZenSession(
      ReadFixture("zen-sessions.json"), ReadFixture("zen-containers.json"));
  CHECK(plan);
  return *plan;
}

const ImportSpace* SpaceNamed(const ImportPlan& plan, const std::string& name) {
  for (const ImportSpace& space : plan.spaces) {
    if (space.name == name) {
      return &space;
    }
  }
  return nullptr;
}

const ImportFolder* FolderNamed(const ImportPlan& plan,
                                const std::string& name) {
  for (const ImportFolder& folder : plan.folders) {
    if (folder.name == name) {
      return &folder;
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

TEST(ZenReaderTest, SpacesKeepTheirOrderNamesAndIcons) {
  const ImportPlan plan = ReadFixturePlan();
  EXPECT_EQ(ImportSourceKind::kZen, plan.source);
  ASSERT_EQ(2u, plan.spaces.size());
  EXPECT_EQ("Personal", plan.spaces[0].name);
  EXPECT_EQ("\xF0\x9F\x8F\xA1", plan.spaces[0].icon);
  EXPECT_EQ("Work", plan.spaces[1].name);
}

// A space that opened its tabs in a container keeps separate logins, named
// after the container; a space with none uses the shared logins.
TEST(ZenReaderTest, AContainerBecomesSeparateLogins) {
  const ImportPlan plan = ReadFixturePlan();
  ASSERT_EQ(1u, plan.profiles.size());
  EXPECT_EQ("Work", plan.profiles[0].name);
  EXPECT_EQ(plan.profiles[0].key, SpaceNamed(plan, "Work")->profile_key);
  EXPECT_EQ("", SpaceNamed(plan, "Personal")->profile_key);
}

// The unpinned tab is left behind, and the folders' hidden placeholders are
// not pages.
TEST(ZenReaderTest, PinnedTabsArriveInOrderAndNothingElse) {
  const ImportPlan plan = ReadFixturePlan();
  const std::string personal = SpaceNamed(plan, "Personal")->key;
  const std::string work = SpaceNamed(plan, "Work")->key;
  EXPECT_EQ((std::vector<std::string>{"https://example.com/articles/one",
                                      "https://example.com/articles/deep",
                                      "https://docs.example.com/"}),
            Urls(plan, ImportEntryKind::kPinned, personal));
  EXPECT_EQ((std::vector<std::string>{"https://work.example.com/board"}),
            Urls(plan, ImportEntryKind::kPinned, work));
  EXPECT_FALSE(EntryAt(plan, "https://news.example.com/today"));
}

// A pinned tab goes back to the address it was pinned at, which is Kyuzen's
// home address, and keeps the name its owner gave it.
TEST(ZenReaderTest, APinnedTabKeepsItsPinnedAddressAndItsName) {
  const ImportPlan plan = ReadFixturePlan();
  const ImportEntry* docs = EntryAt(plan, "https://docs.example.com/");
  ASSERT_TRUE(docs);
  EXPECT_EQ("Team docs", docs->title);
  EXPECT_TRUE(docs->renamed);
  EXPECT_FALSE(EntryAt(plan, "https://example.com/articles/one")->renamed);
  EXPECT_FALSE(EntryAt(plan, "https://docs.example.com/guide"));
}

TEST(ZenReaderTest, FoldersNestAndHoldTheirTabs) {
  const ImportPlan plan = ReadFixturePlan();
  ASSERT_EQ(2u, plan.folders.size());
  const ImportFolder* reading = FolderNamed(plan, "Reading");
  const ImportFolder* deep = FolderNamed(plan, "Deep dive");
  ASSERT_TRUE(reading && deep);
  EXPECT_EQ(SpaceNamed(plan, "Personal")->key, reading->space_key);
  EXPECT_EQ("", reading->parent_key);
  EXPECT_FALSE(reading->collapsed);
  EXPECT_EQ(reading->key, deep->parent_key);
  EXPECT_TRUE(deep->collapsed);
  // Parents come first, so a folder can be made before anything names it.
  EXPECT_EQ(reading, &plan.folders[0]);
  EXPECT_EQ(reading->key,
            EntryAt(plan, "https://example.com/articles/one")->folder_key);
  EXPECT_EQ(deep->key,
            EntryAt(plan, "https://example.com/articles/deep")->folder_key);
  EXPECT_EQ("", EntryAt(plan, "https://docs.example.com/")->folder_key);
}

// Zen shows each space the Essentials of its own container, so each becomes
// a favourite in the spaces on that container.
TEST(ZenReaderTest, EssentialsGoToTheSpacesOnTheirContainer) {
  const ImportPlan plan = ReadFixturePlan();
  EXPECT_EQ((std::vector<std::string>{"https://mail.example.com/"}),
            Urls(plan, ImportEntryKind::kFavorite,
                 SpaceNamed(plan, "Personal")->key));
  EXPECT_EQ(
      (std::vector<std::string>{"https://tracker.example.com/"}),
      Urls(plan, ImportEntryKind::kFavorite, SpaceNamed(plan, "Work")->key));
}

TEST(ZenReaderTest, CountsAreWhatTheSourceShowed) {
  const ImportPlan::Counts counts = ReadFixturePlan().counts();
  EXPECT_EQ(2u, counts.spaces);
  EXPECT_EQ(4u, counts.pinned);
  EXPECT_EQ(2u, counts.favorites);
  EXPECT_EQ(2u, counts.folders);
}

// An Essential on a container no space uses would otherwise vanish; Zen
// shows it everywhere when it keeps Essentials shared, so it goes everywhere.
TEST(ZenReaderTest, AnEssentialNoSpaceClaimsGoesToEverySpace) {
  const std::optional<ImportPlan> plan = ReadZenSession(
      R"({"spaces": [{"uuid": "{a}", "name": "A", "containerTabId": 0},
                     {"uuid": "{b}", "name": "B", "containerTabId": 0}],
          "tabs": [{"pinned": true, "zenEssential": true, "userContextId": 7,
                    "entries": [{"url": "https://x.example/", "title": "X"}]}]})",
      "");
  ASSERT_TRUE(plan);
  EXPECT_EQ(2u, Urls(*plan, ImportEntryKind::kFavorite, "{a}").size() +
                    Urls(*plan, ImportEntryKind::kFavorite, "{b}").size());
  EXPECT_EQ(1u, plan->counts().favorites);
}

// Zen's picked icons are its own images, not emoji, so Kyuzen draws a letter.
TEST(ZenReaderTest, AnIconThatIsNotAnEmojiIsDropped) {
  const std::optional<ImportPlan> plan = ReadZenSession(
      R"({"spaces": [{"uuid": "{a}", "name": "A",
          "icon": "chrome://browser/skin/zen-icons/selectable/star.svg"}]})",
      "");
  ASSERT_TRUE(plan);
  ASSERT_EQ(1u, plan->spaces.size());
  EXPECT_EQ("", plan->spaces[0].icon);
}

// The title falls back through what Zen had: the pinned page's title, the
// current page's title, and finally the address.
TEST(ZenReaderTest, AMissingTitleFallsBackToTheAddress) {
  const std::optional<ImportPlan> plan = ReadZenSession(
      R"({"spaces": [{"uuid": "{a}", "name": "A"}],
          "tabs": [{"pinned": true, "zenWorkspace": "{a}",
                    "entries": [{"url": "https://y.example/"}]}]})",
      "");
  ASSERT_TRUE(plan);
  ASSERT_EQ(1u, plan->entries.size());
  EXPECT_EQ("https://y.example/", plan->entries[0].title);
}

// Only web pages come across: a settings page or an extension's page would
// mean nothing in Kyuzen.
TEST(ZenReaderTest, OnlyWebAddressesArrive) {
  const std::optional<ImportPlan> plan = ReadZenSession(
      R"({"spaces": [{"uuid": "{a}", "name": "A"}],
          "tabs": [{"pinned": true, "zenWorkspace": "{a}",
                    "entries": [{"url": "about:preferences"}]},
                   {"pinned": true, "zenWorkspace": "{a}",
                    "entries": [{"url": "moz-extension://abc/page.html"}]}]})",
      "");
  ASSERT_TRUE(plan);
  EXPECT_TRUE(plan->entries.empty());
}

// A split view inside a folder is not a folder; its tabs land in the folder
// that holds it.
TEST(ZenReaderTest, ASplitViewsTabsLandInItsFolder) {
  const std::optional<ImportPlan> plan = ReadZenSession(
      R"({"spaces": [{"uuid": "{a}", "name": "A"}],
          "folders": [{"id": "f", "name": "F", "workspaceId": "{a}"},
                      {"id": "s", "splitViewGroup": true, "parentId": "f",
                       "workspaceId": "{a}"}],
          "tabs": [{"pinned": true, "zenWorkspace": "{a}", "groupId": "s",
                    "entries": [{"url": "https://left.example/"}]},
                   {"pinned": true, "zenWorkspace": "{a}", "groupId": "s",
                    "entries": [{"url": "https://right.example/"}]}]})",
      "");
  ASSERT_TRUE(plan);
  ASSERT_EQ(1u, plan->folders.size());
  ASSERT_EQ(2u, plan->entries.size());
  EXPECT_EQ(plan->folders[0].key, plan->entries[0].folder_key);
  EXPECT_EQ(plan->folders[0].key, plan->entries[1].folder_key);
}

// Zen keeps the file compressed; decoding it must give the same plan.
TEST(ZenReaderTest, TheCompressedFileReadsLikeThePlainOne) {
  const std::string compressed = ReadFixture("zen-sessions.jsonlz4");
  const std::optional<std::string> json =
      DecodeMozLz4(base::as_byte_span(compressed));
  ASSERT_TRUE(json);
  const std::optional<ImportPlan> plan =
      ReadZenSession(*json, ReadFixture("zen-containers.json"));
  ASSERT_TRUE(plan);
  EXPECT_EQ(ReadFixturePlan().counts().pinned, plan->counts().pinned);
  EXPECT_EQ(2u, plan->spaces.size());
}

// Firefox saves a title as the page gave it, so half an emoji arrives as a
// lone escape; the page still comes across.
TEST(ZenReaderTest, AHalfEmojiInATitleDoesNotSpoilTheFile) {
  const std::optional<ImportPlan> plan = ReadZenSession(
      R"({"spaces": [{"uuid": "{a}", "name": "A"}],
          "tabs": [{"pinned": true, "zenWorkspace": "{a}",
                    "entries": [{"url": "https://z.example/",
                                 "title": "Cut \ud83d"}]}]})",
      "");
  ASSERT_TRUE(plan);
  ASSERT_EQ(1u, plan->entries.size());
  EXPECT_EQ("https://z.example/", plan->entries[0].url.spec());
}

// A damaged file where two folders hold each other must not hang the reader
// or lose either folder.
TEST(ZenReaderTest, FoldersThatHoldEachOtherAreBothKept) {
  const std::optional<ImportPlan> plan = ReadZenSession(
      R"({"spaces": [{"uuid": "{a}", "name": "A"}],
          "folders": [{"id": "x", "name": "X", "parentId": "y",
                       "workspaceId": "{a}"},
                      {"id": "y", "name": "Y", "parentId": "x",
                       "workspaceId": "{a}"}]})",
      "");
  ASSERT_TRUE(plan);
  ASSERT_EQ(2u, plan->folders.size());
  EXPECT_EQ("", plan->folders[0].parent_key);
  EXPECT_EQ(plan->folders[0].key, plan->folders[1].parent_key);
}

TEST(ZenReaderTest, AFileWithNothingInItIsAnEmptyPlan) {
  const std::optional<ImportPlan> plan = ReadZenSession("{}", "");
  ASSERT_TRUE(plan);
  EXPECT_TRUE(plan->spaces.empty());
}

TEST(ZenReaderTest, TextThatIsNotASessionIsRefused) {
  EXPECT_FALSE(ReadZenSession("not json", ""));
  EXPECT_FALSE(ReadZenSession("[1, 2]", ""));
}

}  // namespace
}  // namespace arcium
