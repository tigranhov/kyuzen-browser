// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/import_applier.h"

#include <string>
#include <vector>

#include "arcium/browser/import/import_plan.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/folder.h"
#include "arcium/browser/model/tab_entry.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

// Two spaces, one on a container of its own, with a folder two deep, a
// renamed pinned tab and a favourite shared by both.
ImportPlan TwoSpaces() {
  ImportPlan plan;
  plan.profiles = {{"c2", "Work logins"}};
  plan.spaces = {{"home", "Home", "\xF0\x9F\x8F\xA1", ""},
                 {"work", "Work", "", "c2"}};
  plan.folders = {{"f1", "home", "", "Reading", false},
                  {"f2", "home", "f1", "Deep dive", true}};
  plan.entries = {
      {ImportEntryKind::kFavorite, "home", "", GURL("https://mail.example/"),
       "Mail"},
      {ImportEntryKind::kFavorite, "work", "", GURL("https://mail.example/"),
       "Mail"},
      {ImportEntryKind::kPinned, "home", "f1", GURL("https://one.example/"),
       "One"},
      {ImportEntryKind::kPinned, "home", "f2", GURL("https://deep.example/"),
       "Deep"},
      {ImportEntryKind::kPinned, "home", "", GURL("https://docs.example/"),
       "Team docs", true},
      {ImportEntryKind::kPinned, "work", "", GURL("https://board.example/"),
       "Board"},
  };
  return plan;
}

const Space* SpaceNamed(const ArciumModel& model, const char* name) {
  for (const Space& space : model.spaces()) {
    if (space.name == base::UTF8ToUTF16(name)) {
      return &space;
    }
  }
  return nullptr;
}

const TabEntry* EntryAt(const ArciumModel& model, const char* url) {
  for (const TabEntry& entry : model.entries()) {
    if (entry.url == GURL(url)) {
      return &entry;
    }
  }
  return nullptr;
}

const Folder* FolderNamed(const ArciumModel& model, const char* name) {
  for (const Folder& folder : model.folders()) {
    if (folder.name == base::UTF8ToUTF16(name)) {
      return &folder;
    }
  }
  return nullptr;
}

ImportChoices Choices(const ImportPlan& plan) {
  ImportChoices choices = DefaultChoices(plan);
  choices.profile_colors = 8;
  return choices;
}

TEST(ImportApplierTest, SpacesArriveWithTheirIconsAndLogins) {
  ArciumModel model;
  const ImportPlan plan = TwoSpaces();
  const ImportResult result = ApplyImportPlan(plan, Choices(plan), model);
  EXPECT_EQ(2u, result.spaces);
  EXPECT_EQ(1u, result.profiles);
  const Space* home = SpaceNamed(model, "Home");
  const Space* work = SpaceNamed(model, "Work");
  ASSERT_TRUE(home && work);
  EXPECT_EQ(u"\U0001F3E1", home->icon);
  EXPECT_EQ(DefaultProfileId(), home->profile_id);
  const ArciumProfile* logins = model.GetProfile(work->profile_id);
  ASSERT_TRUE(logins);
  EXPECT_EQ(u"Work logins", logins->name);
  EXPECT_NE(0, logins->color);
  EXPECT_EQ(home->id, result.first_space);
}

TEST(ImportApplierTest, FoldersNestAndHoldTheirTabs) {
  ArciumModel model;
  const ImportPlan plan = TwoSpaces();
  ApplyImportPlan(plan, Choices(plan), model);
  const Folder* reading = FolderNamed(model, "Reading");
  const Folder* deep = FolderNamed(model, "Deep dive");
  ASSERT_TRUE(reading && deep);
  EXPECT_EQ(reading->id, deep->parent_id);
  EXPECT_TRUE(deep->collapsed);
  EXPECT_FALSE(reading->collapsed);
  EXPECT_EQ(reading->id, EntryAt(model, "https://one.example/")->folder_id);
  EXPECT_EQ(deep->id, EntryAt(model, "https://deep.example/")->folder_id);
  EXPECT_FALSE(EntryAt(model, "https://docs.example/")->folder_id);
}

// Entries arrive cold, with the titles they had, and a name their owner gave
// stays when the page later calls itself something else.
TEST(ImportApplierTest, EntriesKeepTheirTitlesAndARenameSticks) {
  ArciumModel model;
  const ImportPlan plan = TwoSpaces();
  ApplyImportPlan(plan, Choices(plan), model);
  const TabEntry* docs = EntryAt(model, "https://docs.example/");
  ASSERT_TRUE(docs);
  EXPECT_EQ(EntryKind::kPinned, docs->kind);
  EXPECT_EQ(u"Team docs", docs->custom_title);
  const TabEntry* one = EntryAt(model, "https://one.example/");
  EXPECT_EQ(u"One", one->last_title);
  EXPECT_TRUE(one->custom_title.empty());
}

TEST(ImportApplierTest, FavouritesArriveInEverySpaceTheyWereShownIn) {
  ArciumModel model;
  const ImportPlan plan = TwoSpaces();
  const ImportResult result = ApplyImportPlan(plan, Choices(plan), model);
  EXPECT_EQ(2u, result.favorites);
  EXPECT_EQ(
      1u,
      model.EntriesForKind(SpaceNamed(model, "Home")->id, EntryKind::kFavorite)
          .size());
  EXPECT_EQ(
      1u,
      model.EntriesForKind(SpaceNamed(model, "Work")->id, EntryKind::kFavorite)
          .size());
}

// Importing the same thing twice adds nothing the second time.
TEST(ImportApplierTest, ASecondImportAddsNothing) {
  ArciumModel model;
  const ImportPlan plan = TwoSpaces();
  ApplyImportPlan(plan, Choices(plan), model);
  const size_t spaces = model.spaces().size();
  const size_t entries = model.entries().size();
  const size_t folders = model.folders().size();
  const size_t profiles = model.profiles().size();
  const ImportResult again = ApplyImportPlan(plan, Choices(plan), model);
  EXPECT_EQ(0u, again.spaces + again.profiles + again.folders + again.pinned +
                    again.favorites);
  EXPECT_EQ(spaces, model.spaces().size());
  EXPECT_EQ(entries, model.entries().size());
  EXPECT_EQ(folders, model.folders().size());
  EXPECT_EQ(profiles, model.profiles().size());
}

// A folder deeper than Kyuzen draws hands its tabs to the deepest folder it
// may have, rather than losing them or nesting past the edge of the sidebar.
TEST(ImportApplierTest, AFolderTooDeepJoinsTheDeepestLevel) {
  ImportPlan plan;
  plan.spaces = {{"s", "S", "", ""}};
  std::string parent;
  for (int level = 0; level < kMaxFolderDepth + 2; ++level) {
    const std::string key = "f" + std::to_string(level);
    plan.folders.push_back(
        {key, "s", parent, "Level " + std::to_string(level), false});
    parent = key;
  }
  plan.entries = {{ImportEntryKind::kPinned, "s", parent,
                   GURL("https://bottom.example/"), "Bottom"}};
  ArciumModel model;
  const ImportResult result = ApplyImportPlan(plan, Choices(plan), model);
  EXPECT_EQ(static_cast<size_t>(kMaxFolderDepth), result.folders);
  const TabEntry* bottom = EntryAt(model, "https://bottom.example/");
  ASSERT_TRUE(bottom && bottom->folder_id);
  EXPECT_EQ(kMaxFolderDepth - 1, model.FolderDepth(*bottom->folder_id));
}

// The one empty space a fresh install starts with is handed back for
// removal once the import has brought spaces of its own.
TEST(ImportApplierTest, TheEmptyStarterSpaceIsHandedBack) {
  ArciumModel model;
  const SpaceId starter = model.spaces()[0].id;
  const ImportPlan plan = TwoSpaces();
  EXPECT_EQ(starter, ApplyImportPlan(plan, Choices(plan), model).empty_starter);
}

// A space someone has put something in is theirs, fresh install or not.
TEST(ImportApplierTest, AStarterSpaceInUseIsKept) {
  ArciumModel model;
  model.AddEntry(model.spaces()[0].id, EntryKind::kPinned,
                 GURL("https://mine.example/"), u"Mine");
  const ImportPlan plan = TwoSpaces();
  EXPECT_FALSE(
      ApplyImportPlan(plan, Choices(plan), model).empty_starter.is_valid());
}

// A source with a space named like the starter fills it, and a space with
// something in it is not handed back for removal.
TEST(ImportApplierTest, AStarterSpaceTheImportFillsIsKept) {
  ArciumModel model;
  const std::u16string starter_name = model.spaces()[0].name;
  ImportPlan plan;
  plan.spaces = {{"w", "Work", "", ""},
                 {"s", base::UTF16ToUTF8(starter_name), "", ""}};
  plan.entries = {{ImportEntryKind::kPinned, "s", "",
                   GURL("https://kept.example/"), "Kept"}};
  EXPECT_FALSE(
      ApplyImportPlan(plan, Choices(plan), model).empty_starter.is_valid());
}

// Separate logins are the person's choice on the second step, either way.
TEST(ImportApplierTest, TheLoginsChoiceIsHonouredBothWays) {
  ArciumModel model;
  const ImportPlan plan = TwoSpaces();
  ImportChoices choices = Choices(plan);
  choices.separate_logins = {"home"};
  const ImportResult result = ApplyImportPlan(plan, choices, model);
  EXPECT_EQ(1u, result.profiles);
  EXPECT_EQ(DefaultProfileId(), SpaceNamed(model, "Work")->profile_id);
  const ArciumProfile* home =
      model.GetProfile(SpaceNamed(model, "Home")->profile_id);
  ASSERT_TRUE(home);
  EXPECT_EQ(u"Home", home->name);
}

// Two spaces on one container share one profile, as they shared the logins.
TEST(ImportApplierTest, SpacesThatSharedLoginsShareAProfile) {
  ImportPlan plan;
  plan.profiles = {{"c", "Client"}};
  plan.spaces = {{"a", "A", "", "c"}, {"b", "B", "", "c"}};
  ArciumModel model;
  const ImportResult result = ApplyImportPlan(plan, Choices(plan), model);
  EXPECT_EQ(1u, result.profiles);
  EXPECT_EQ(SpaceNamed(model, "A")->profile_id,
            SpaceNamed(model, "B")->profile_id);
}

TEST(ImportApplierTest, UntickedKindsStayBehind) {
  ArciumModel model;
  const ImportPlan plan = TwoSpaces();
  ImportChoices choices = Choices(plan);
  choices.favorites = false;
  choices.folders = false;
  const ImportResult result = ApplyImportPlan(plan, choices, model);
  EXPECT_EQ(0u, result.favorites);
  EXPECT_EQ(0u, result.folders);
  EXPECT_EQ(4u, result.pinned);
  EXPECT_TRUE(model.folders().empty());
  EXPECT_FALSE(EntryAt(model, "https://deep.example/")->folder_id);
}

// Without spaces, everything lands in the space the person is looking at.
TEST(ImportApplierTest, WithoutSpacesEverythingLandsInOne) {
  ArciumModel model;
  const SpaceId here = model.spaces()[0].id;
  const ImportPlan plan = TwoSpaces();
  ImportChoices choices = Choices(plan);
  choices.spaces = false;
  choices.into = here;
  const ImportResult result = ApplyImportPlan(plan, choices, model);
  EXPECT_EQ(1u, model.spaces().size());
  EXPECT_EQ(0u, result.profiles);
  EXPECT_EQ(4u, model.EntriesForKind(here, EntryKind::kPinned).size());
  // The favourite both spaces showed arrives once.
  EXPECT_EQ(1u, model.EntriesForKind(here, EntryKind::kFavorite).size());
  EXPECT_FALSE(result.empty_starter.is_valid());
}

}  // namespace
}  // namespace arcium
