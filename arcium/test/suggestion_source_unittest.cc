// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Turning Chrome's answers into rows the box can draw. The half worth testing
// on its own is the pure one: a result in, rows out, no profile and no
// network anywhere near it.

#include "arcium/ui/browser/suggestion_source.h"

#include <string>
#include <vector>

#include "arcium/ui/browser/command_box_row.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/vector_icons/vector_icons.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_base.h"
#include "url/gurl.h"

namespace arcium {
namespace {

AutocompleteMatch MakeMatch(const std::string& url,
                            const std::u16string& description,
                            bool open_tab) {
  AutocompleteMatch match(nullptr, 1000, false,
                          AutocompleteMatchType::HISTORY_URL);
  match.destination_url = GURL(url);
  match.contents = base::UTF8ToUTF16(url);
  match.description = description;
  match.has_tab_match = open_tab;
  return match;
}

TEST(SuggestionSourceTest, KeepsTheControllersOrder) {
  AutocompleteResult result;
  ACMatches matches;
  matches.push_back(MakeMatch("https://a.test/", u"A", false));
  matches.push_back(MakeMatch("https://b.test/", u"B", false));
  result.AppendMatches(matches);

  const std::vector<SuggestionRow> rows = RowsForResult(result);
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(u"A", rows[0].title);
  EXPECT_EQ(GURL("https://b.test/"), rows[1].destination);
}

TEST(SuggestionSourceTest, SaysWhichRowIsATabYouAlreadyHave) {
  AutocompleteResult result;
  ACMatches matches;
  matches.push_back(MakeMatch("https://a.test/", u"A", true));
  matches.push_back(MakeMatch("https://b.test/", u"B", false));
  result.AppendMatches(matches);

  const std::vector<SuggestionRow> rows = RowsForResult(result);
  ASSERT_EQ(2u, rows.size());
  EXPECT_TRUE(rows[0].is_open_tab);
  EXPECT_FALSE(rows[1].is_open_tab);
}

TEST(SuggestionSourceTest, ARowWithNowhereToGoIsNotARow) {
  AutocompleteResult result;
  ACMatches matches;
  matches.push_back(MakeMatch("", u"nowhere", false));
  result.AppendMatches(matches);
  EXPECT_TRUE(RowsForResult(result).empty());
}

TEST(SuggestionSourceTest, FallsBackToTheAddressWhenThereIsNoTitle) {
  AutocompleteResult result;
  ACMatches matches;
  matches.push_back(MakeMatch("https://a.test/", u"", false));
  result.AppendMatches(matches);
  const std::vector<SuggestionRow> rows = RowsForResult(result);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(u"https://a.test/", rows[0].title);
}

SuggestionRow Row(const std::string& url, bool open_tab) {
  SuggestionRow row;
  row.destination = GURL(url);
  row.title = base::UTF8ToUTF16(url);
  row.is_open_tab = open_tab;
  return row;
}

TEST(MergeSuggestionsTest, WhatYouAlreadyHaveComesFirst) {
  std::vector<SuggestionRow> mine = {Row("https://tab.test/", true)};
  std::vector<SuggestionRow> web = {Row("https://history.test/", false)};
  const std::vector<SuggestionRow> merged =
      MergeSuggestions(std::move(mine), std::move(web));
  ASSERT_EQ(2u, merged.size());
  EXPECT_EQ(GURL("https://tab.test/"), merged[0].destination);
  EXPECT_EQ(GURL("https://history.test/"), merged[1].destination);
}

TEST(MergeSuggestionsTest, TheSamePageIsOfferedOnce) {
  std::vector<SuggestionRow> mine = {Row("https://a.test/", true)};
  std::vector<SuggestionRow> web = {Row("https://a.test/", false),
                                    Row("https://b.test/", false)};
  const std::vector<SuggestionRow> merged =
      MergeSuggestions(std::move(mine), std::move(web));
  ASSERT_EQ(2u, merged.size());
  // And it keeps the half that knows it is open, not the half that does not.
  EXPECT_TRUE(merged[0].is_open_tab);
  EXPECT_EQ(GURL("https://b.test/"), merged[1].destination);
}

TEST(MergeSuggestionsTest, EitherHalfMayBeEmpty) {
  EXPECT_EQ(1u, MergeSuggestions({Row("https://a.test/", true)}, {}).size());
  EXPECT_EQ(1u, MergeSuggestions({}, {Row("https://a.test/", false)}).size());
  EXPECT_TRUE(MergeSuggestions({}, {}).empty());
}

// A row is a view, and a view's labels need fonts and a UI thread.
using CommandBoxRowTest = views::ViewsTestBase;

// The glyph a row was drawn with, or null when it shows something else.
const gfx::VectorIcon* KindIconOf(CommandBoxRow& row) {
  const ui::ImageModel& image = row.icon_for_testing()->GetImageModel();
  return image.IsVectorIcon() ? image.GetVectorIcon().vector_icon() : nullptr;
}

TEST_F(CommandBoxRowTest, EachKindOfRowHasItsOwnGlyph) {
  SuggestionRow page;
  page.title = u"Example";
  page.destination = GURL("https://example.com/");
  CommandBoxRow page_row(page, u"", base::DoNothing());
  EXPECT_EQ(&vector_icons::kGlobeIcon, KindIconOf(page_row));

  SuggestionRow search = page;
  search.is_search = true;
  CommandBoxRow search_row(search, u"", base::DoNothing());
  EXPECT_EQ(&vector_icons::kSearchIcon, KindIconOf(search_row));

  SuggestionRow command;
  command.title = u"New tab";
  command.command_id = 1;
  CommandBoxRow command_row(command, u"", base::DoNothing());
  EXPECT_EQ(&vector_icons::kKeyboardIcon, KindIconOf(command_row));
}

TEST_F(CommandBoxRowTest, ATabYouHaveSaysEnterSwitchesToIt) {
  SuggestionRow row;
  row.title = u"Example";
  row.destination = GURL("https://example.com/");
  row.is_open_tab = true;
  // A tab wins over a shortcut: what Enter does is the thing to say.
  CommandBoxRow open_tab(row, u"\u2318T", base::DoNothing());
  ASSERT_TRUE(open_tab.aside_for_testing());
  EXPECT_EQ(u"Switch to tab", open_tab.aside_for_testing()->GetText());

  row.is_open_tab = false;
  CommandBoxRow page(row, u"", base::DoNothing());
  EXPECT_FALSE(page.aside_for_testing())
      << "a page you do not have says something at its edge";
}

TEST_F(CommandBoxRowTest, ACommandShowsItsShortcut) {
  SuggestionRow row;
  row.title = u"New tab";
  row.command_id = 1;
  CommandBoxRow command(row, u"\u2318T", base::DoNothing());
  ASSERT_TRUE(command.aside_for_testing());
  EXPECT_EQ(u"\u2318T", command.aside_for_testing()->GetText());
}

TEST_F(CommandBoxRowTest, OnlyTheSelectedRowShowsTheEnterMark) {
  SuggestionRow row;
  row.title = u"Example";
  row.destination = GURL("https://example.com/");
  CommandBoxRow view(row, u"", base::DoNothing());
  EXPECT_FALSE(view.enter_mark_for_testing()->GetVisible());
  view.SetSelected(true);
  EXPECT_TRUE(view.enter_mark_for_testing()->GetVisible());
  view.SetSelected(false);
  EXPECT_FALSE(view.enter_mark_for_testing()->GetVisible());
}

}  // namespace
}  // namespace arcium
