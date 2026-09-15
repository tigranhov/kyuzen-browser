// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/box_commands.h"

#include <string>
#include <vector>

#include "arcium/ui/browser/suggestion_source.h"
#include "chrome/app/chrome_command_ids.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

std::vector<int> IdsFor(std::u16string_view query) {
  std::vector<int> ids;
  for (const BoxCommand* command : MatchBoxCommands(query)) {
    ids.push_back(command->id);
  }
  return ids;
}

TEST(BoxCommandsTest, TheStartOfAWordOffersItsCommand) {
  // "closed" starts with "clo" too, and so does the "clone" that duplicating
  // a tab goes by, so all three are offered.
  EXPECT_EQ(
      (std::vector<int>{IDC_CLOSE_TAB, IDC_RESTORE_TAB, IDC_DUPLICATE_TAB}),
      IdsFor(u"clo"));
  EXPECT_EQ(std::vector<int>{IDC_SHOW_DOWNLOADS}, IdsFor(u"downl"));
}

TEST(BoxCommandsTest, CaseIsIgnored) {
  EXPECT_EQ(std::vector<int>{IDC_SHOW_DOWNLOADS}, IdsFor(u"DoWnL"));
}

TEST(BoxCommandsTest, EveryWordTypedMustStartAWordOfTheSameCommand) {
  EXPECT_EQ(std::vector<int>{IDC_DEV_TOOLS}, IdsFor(u"dev to"));
  EXPECT_TRUE(IdsFor(u"dev tab").empty());
}

TEST(BoxCommandsTest, TheMiddleOfAWordOffersNothing) {
  EXPECT_TRUE(IdsFor(u"ools").empty());
}

TEST(BoxCommandsTest, AnExtraNameOffersItsCommandWithoutBeingShown) {
  EXPECT_EQ(std::vector<int>{IDC_DEV_TOOLS}, IdsFor(u"inspect"));
  const std::vector<const BoxCommand*> settings = MatchBoxCommands(u"prefer");
  ASSERT_EQ(1u, settings.size());
  EXPECT_EQ(u"Settings", settings[0]->name);
}

TEST(BoxCommandsTest, OneLetterOffersNothing) {
  EXPECT_TRUE(IdsFor(u"c").empty());
  EXPECT_TRUE(IdsFor(u"  c ").empty());
  EXPECT_TRUE(IdsFor(u"").empty());
}

TEST(BoxCommandsTest, NoMoreThanThreeCommandsAreOffered) {
  // "ta" starts "tab" three times and "task" once.
  EXPECT_EQ(kMaxCommandRows, IdsFor(u"ta").size());
}

TEST(BoxCommandsTest, ArciumsOwnCommandsAreOfferedToo) {
  EXPECT_EQ(std::vector<int>{kBoxCommandNewSpace}, IdsFor(u"new sp"));
  EXPECT_EQ(std::vector<int>{kBoxCommandSiteSearch}, IdsFor(u"site"));
}

TEST(BoxCommandsTest, ASiteSearchRowNamesTheSite) {
  EXPECT_EQ(u"Search YouTube for cats", SiteSearchTitle(u"YouTube", u"cats"));
}

TEST(ComposeRowsTest, CommandsComeFirstAndSurviveHavingNoAddress) {
  SuggestionRow command;
  command.title = u"Close tab";
  command.command_id = IDC_CLOSE_TAB;
  SuggestionRow mine;
  mine.destination = GURL("https://a.test/");
  SuggestionRow web;
  web.destination = GURL("https://b.test/");

  const std::vector<SuggestionRow> rows = ComposeRows({command}, {mine}, {web});

  ASSERT_EQ(3u, rows.size());
  ASSERT_TRUE(rows[0].command_id.has_value());
  EXPECT_EQ(IDC_CLOSE_TAB, *rows[0].command_id);
  EXPECT_EQ(GURL("https://a.test/"), rows[1].destination);
  EXPECT_EQ(GURL("https://b.test/"), rows[2].destination);
}

TEST(ComposeRowsTest, RowsForCommandsCarryTheirIds) {
  const std::vector<SuggestionRow> rows = RowsForCommands(u"dupl");
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(u"Duplicate tab", rows[0].title);
  ASSERT_TRUE(rows[0].command_id.has_value());
  EXPECT_EQ(IDC_DUPLICATE_TAB, *rows[0].command_id);
  EXPECT_FALSE(rows[0].destination.is_valid());
}

}  // namespace
}  // namespace arcium
