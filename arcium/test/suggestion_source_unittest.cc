// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Turning Chrome's answers into rows the box can draw. The half worth testing
// on its own is the pure one: a result in, rows out, no profile and no
// network anywhere near it.

#include "arcium/ui/browser/suggestion_source.h"

#include <string>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "testing/gtest/include/gtest/gtest.h"
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

}  // namespace
}  // namespace arcium
