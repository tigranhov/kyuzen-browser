// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/welcome_sources.h"

#include <string>
#include <utility>
#include <vector>

#include "arcium/browser/import/import_applier.h"
#include "arcium/browser/import/import_finder.h"
#include "arcium/browser/import/import_plan.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/profile_defaults.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// A made-up source: `spaces` spaces named after the letters, the first with
// a container of its own, each holding `pinned` pinned pages.
FoundSource Source(ImportSourceKind kind,
                   std::string profile_name,
                   size_t spaces,
                   size_t pinned) {
  FoundSource source;
  source.kind = kind;
  source.profile_name = std::move(profile_name);
  source.plan.source = kind;
  for (size_t s = 0; s < spaces; ++s) {
    ImportSpace space;
    space.key = "space-" + std::to_string(s);
    space.name = std::string(1, static_cast<char>('A' + s));
    if (s == 0) {
      space.profile_key = "container-1";
    }
    source.plan.spaces.push_back(space);
    for (size_t p = 0; p < pinned; ++p) {
      ImportEntry entry;
      entry.space_key = space.key;
      entry.url =
          GURL("https://example.com/" + space.key + "/" + std::to_string(p));
      entry.title = "Page";
      source.plan.AddEntry(std::move(entry));
    }
  }
  if (spaces > 0) {
    source.plan.profiles.push_back({"container-1", "Work"});
  }
  return source;
}

TEST(WelcomeResumeStepTest, AFreshInstallStartsAtTheFirstStep) {
  EXPECT_EQ(WelcomeStep::kSetup,
            WelcomeResumeStep(kWelcomeNotStarted, /*model_file_absent=*/true));
}

TEST(WelcomeResumeStepTest, SomeoneUpdatingNeverSeesIt) {
  EXPECT_EQ(std::nullopt,
            WelcomeResumeStep(kWelcomeNotStarted, /*model_file_absent=*/false));
}

TEST(WelcomeResumeStepTest, AQuitComesBackAtTheStepReached) {
  const int search = static_cast<int>(WelcomeStep::kSearch) + 1;
  EXPECT_EQ(WelcomeStep::kSearch, WelcomeResumeStep(search, false));
  EXPECT_EQ(WelcomeStep::kSearch, WelcomeResumeStep(search, true));
  EXPECT_EQ(WelcomeStep::kDone,
            WelcomeResumeStep(static_cast<int>(WelcomeStep::kDone) + 1, false));
}

TEST(WelcomeResumeStepTest, DoneIsDoneEvenWithoutAModelFile) {
  // Finishing with nothing changed writes no model file, and the next launch
  // must not take that for a fresh install.
  EXPECT_EQ(std::nullopt, WelcomeResumeStep(kWelcomeDone, true));
}

TEST(WelcomeResumeStepTest, ANumberNoBuildWroteCountsAsNotStarted) {
  EXPECT_EQ(std::nullopt, WelcomeResumeStep(57, false));
  EXPECT_EQ(WelcomeStep::kSetup, WelcomeResumeStep(57, true));
  EXPECT_EQ(std::nullopt, WelcomeResumeStep(-1, false));
}

TEST(WelcomeSourcesTest, ZenProfilesShareOneRowAndArcComesAfter) {
  std::vector<FoundSource> found;
  found.push_back(Source(ImportSourceKind::kArc, "", 2, 1));
  found.push_back(Source(ImportSourceKind::kZen, "Default", 3, 2));
  found.push_back(Source(ImportSourceKind::kZen, "Research", 1, 5));
  const std::vector<std::vector<FoundSource>> groups =
      GroupByBrowser(std::move(found));
  ASSERT_EQ(2u, groups.size());
  ASSERT_EQ(2u, groups[0].size());
  EXPECT_EQ(ImportSourceKind::kZen, groups[0][0].kind);
  EXPECT_EQ("Default", groups[0][0].profile_name);
  EXPECT_EQ("Research", groups[0][1].profile_name);
  ASSERT_EQ(1u, groups[1].size());
  EXPECT_EQ(ImportSourceKind::kArc, groups[1][0].kind);
}

TEST(WelcomeSourcesTest, ABrowserWithSeveralProfilesOffersThemByName) {
  std::vector<FoundSource> zen;
  zen.push_back(Source(ImportSourceKind::kZen, "Default", 3, 2));
  zen.push_back(Source(ImportSourceKind::kZen, "Research", 1, 5));

  WelcomeSource shown = DescribeSource(zen, 0, u"Found on this Mac");
  EXPECT_EQ(u"Zen", shown.name);
  EXPECT_EQ(u"Found on this Mac", shown.detail);
  EXPECT_EQ(std::vector<std::u16string>({u"Default", u"Research"}),
            shown.profiles);
  EXPECT_EQ(3u, shown.spaces);
  EXPECT_EQ(6u, shown.pinned);

  // The counts follow the profile chosen.
  shown = DescribeSource(zen, 1, u"Found on this Mac");
  EXPECT_EQ(1u, shown.profile);
  EXPECT_EQ(1u, shown.spaces);
  EXPECT_EQ(5u, shown.pinned);
}

TEST(WelcomeSourcesTest, OneProfileOffersNoChoice) {
  std::vector<FoundSource> arc;
  arc.push_back(Source(ImportSourceKind::kArc, "", 2, 1));
  const WelcomeSource shown = DescribeSource(arc, 0, u"StorableSidebar.json");
  EXPECT_EQ(u"Arc", shown.name);
  EXPECT_EQ(WelcomeSource::Kind::kArc, shown.kind);
  EXPECT_TRUE(shown.profiles.empty());
}

TEST(WelcomeSourcesTest, SpacesKeepTheirLoginsApartWhereTheSourceDid) {
  const FoundSource zen = Source(ImportSourceKind::kZen, "Default", 2, 3);
  ImportChoices choices = DefaultChoices(zen.plan);
  std::vector<WelcomeSpace> spaces =
      SpacesToImport(zen.plan, choices, u"from Zen");
  ASSERT_EQ(2u, spaces.size());
  EXPECT_EQ(u"A", spaces[0].name);
  EXPECT_EQ(3u, spaces[0].pinned);
  EXPECT_EQ(u"from Zen", spaces[0].origin);
  EXPECT_TRUE(spaces[0].had_separate_logins);
  EXPECT_TRUE(spaces[0].separate_logins);
  EXPECT_FALSE(spaces[1].had_separate_logins);
  EXPECT_FALSE(spaces[1].separate_logins);

  // A toggle turned off stays off, and one turned on shows on.
  choices.separate_logins = {"space-1"};
  spaces = SpacesToImport(zen.plan, choices, u"from Zen");
  EXPECT_FALSE(spaces[0].separate_logins);
  EXPECT_TRUE(spaces[0].had_separate_logins);
  EXPECT_TRUE(spaces[1].separate_logins);
}

TEST(WelcomeSourcesTest, StartFreshMakesPersonalAndWorkWithNothingInThem) {
  const ImportPlan plan = StartFreshPlan();
  ASSERT_EQ(2u, plan.spaces.size());
  EXPECT_EQ("Personal", plan.spaces[0].name);
  EXPECT_EQ("Work", plan.spaces[1].name);
  EXPECT_TRUE(plan.entries.empty());
  EXPECT_TRUE(plan.folders.empty());
  EXPECT_TRUE(plan.profiles.empty());

  // Applied to a fresh model, the empty starter is handed back to go.
  ArciumModel model;
  const ImportResult result =
      ApplyImportPlan(plan, DefaultChoices(plan), model);
  EXPECT_EQ(2u, result.spaces);
  EXPECT_TRUE(result.empty_starter.is_valid());
}

TEST(WelcomeSourcesTest, TheArrivedSpaceShowsFoldersThenLoosePages) {
  ArciumModel model;
  const SpaceId space = model.AddSpace(u"Work");
  model.AddEntry(space, EntryKind::kPinned, GURL("https://a.example/"),
                 u"Team board");
  const EntryId filed = model.AddEntry(
      space, EntryKind::kPinned, GURL("https://b.example/"), u"Filed away");
  model.AddEntry(space, EntryKind::kPinned, GURL("https://c.example/"), u"");
  model.AddEntry(space, EntryKind::kFavorite, GURL("https://d.example/"),
                 u"A favourite");
  const FolderId folder = model.AddFolder(space, u"Resources");
  model.SetEntryFolder(filed, folder);

  const WelcomeSpace arrived = SpaceAsArrived(model, space, u"from Arc");
  EXPECT_EQ(u"Work", arrived.name);
  EXPECT_EQ(u"from Arc", arrived.origin);
  EXPECT_EQ(3u, arrived.pinned);
  ASSERT_EQ(3u, arrived.rows.size());
  EXPECT_EQ(u"Resources", arrived.rows[0].title);
  EXPECT_TRUE(arrived.rows[0].folder);
  EXPECT_EQ(u"Team board", arrived.rows[1].title);
  EXPECT_FALSE(arrived.rows[1].folder);
  // A page with no title yet shows where it goes.
  EXPECT_EQ(u"c.example", arrived.rows[2].title);
}

TEST(WelcomeSourcesTest, TheArrivedPictureStopsAtFourRows) {
  ArciumModel model;
  const SpaceId space = model.AddSpace(u"Work");
  for (int i = 0; i < 6; ++i) {
    model.AddEntry(space, EntryKind::kPinned,
                   GURL("https://example.com/" + std::to_string(i)), u"Page");
  }
  EXPECT_EQ(4u, SpaceAsArrived(model, space, u"").rows.size());
  EXPECT_EQ(6u, SpaceAsArrived(model, space, u"").pinned);
}

}  // namespace
}  // namespace arcium
