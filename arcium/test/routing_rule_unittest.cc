// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/routing_rule.h"

#include <optional>
#include <utility>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/model_migration.h"
#include "arcium/browser/model/model_serializer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

TEST(RoutingRuleSiteTest, ASiteIsTheHostWithoutWwwInLowerCase) {
  EXPECT_EQ("github.com", RuleSiteForUrl(GURL("https://WWW.GitHub.com/x")));
  EXPECT_EQ("gist.github.com", RuleSiteForUrl(GURL("https://gist.github.com")));
}

TEST(RoutingRuleSiteTest, AnAddressThatIsNotAWebPageHasNoSite) {
  EXPECT_EQ("", RuleSiteForUrl(GURL("chrome://settings")));
  EXPECT_EQ("", RuleSiteForUrl(GURL("file:///tmp/a.html")));
  EXPECT_EQ("", RuleSiteForUrl(GURL()));
}

TEST(RoutingRuleSiteTest, AWwwThatIsTheWholeNameIsKept) {
  // "www.com" is a site of its own, not an empty one.
  EXPECT_EQ("www.com", RuleSiteForUrl(GURL("https://www.com/")));
}

TEST(RoutingRuleMatchTest, ASiteMatchesItselfAndItsSubdomains) {
  EXPECT_TRUE(SiteMatchesUrl("github.com", GURL("https://github.com/a")));
  EXPECT_TRUE(SiteMatchesUrl("github.com", GURL("https://www.github.com/")));
  EXPECT_TRUE(SiteMatchesUrl("github.com", GURL("http://gist.github.com/")));
}

TEST(RoutingRuleMatchTest, ASiteDoesNotMatchANameThatMerelyEndsInIt) {
  EXPECT_FALSE(SiteMatchesUrl("github.com", GURL("https://notgithub.com/")));
  EXPECT_FALSE(SiteMatchesUrl("github.com", GURL("https://github.com.evil/")));
}

TEST(RoutingRuleMatchTest, ASiteNeverMatchesAPageThatIsNotOnTheWeb) {
  EXPECT_FALSE(SiteMatchesUrl("github.com", GURL("file:///github.com")));
  EXPECT_FALSE(SiteMatchesUrl("", GURL("https://github.com/")));
}

TEST(RoutingRuleMatchTest, TheLongestMatchingSiteWins) {
  const SpaceId work = SpaceId::Generate();
  const SpaceId home = SpaceId::Generate();
  const std::vector<RoutingRule> rules = {{"github.com", work},
                                          {"gist.github.com", home}};
  const RoutingRule* gist =
      BestRuleForUrl(rules, GURL("https://gist.github.com/x"));
  ASSERT_TRUE(gist);
  EXPECT_EQ(home, gist->space_id);
  const RoutingRule* main = BestRuleForUrl(rules, GURL("https://github.com/"));
  ASSERT_TRUE(main);
  EXPECT_EQ(work, main->space_id);
  EXPECT_FALSE(BestRuleForUrl(rules, GURL("https://example.com/")));
}

class RoutingRuleModelTest : public testing::Test {
 protected:
  ArciumModel model_;
};

TEST_F(RoutingRuleModelTest, ARuleSendsItsSiteToItsSpace) {
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetRoutingRule("github.com", work);
  EXPECT_EQ(work, model_.SpaceForUrl(GURL("https://gist.github.com/")));
  EXPECT_FALSE(model_.SpaceForUrl(GURL("https://example.com/")).is_valid());
}

TEST_F(RoutingRuleModelTest, SettingASiteAgainMovesItsRule) {
  const SpaceId work = model_.AddSpace(u"Work");
  const SpaceId play = model_.AddSpace(u"Play");
  model_.SetRoutingRule("github.com", work);
  model_.SetRoutingRule("github.com", play);
  ASSERT_EQ(1u, model_.routing_rules().size());
  EXPECT_EQ(play, model_.SpaceForUrl(GURL("https://github.com/")));
}

TEST_F(RoutingRuleModelTest, ASiteIsStoredTheWayItIsMatched) {
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetRoutingRule("WWW.GitHub.com", work);
  ASSERT_EQ(1u, model_.routing_rules().size());
  EXPECT_EQ("github.com", model_.routing_rules()[0].site);
}

TEST_F(RoutingRuleModelTest, ARuleForAnUnknownSpaceOrNoSiteIsRefused) {
  model_.SetRoutingRule("github.com", SpaceId::Generate());
  model_.SetRoutingRule("", model_.default_space_id());
  EXPECT_TRUE(model_.routing_rules().empty());
}

TEST_F(RoutingRuleModelTest, RemovingARuleStopsIt) {
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetRoutingRule("github.com", work);
  model_.RemoveRoutingRule("www.github.com");
  EXPECT_TRUE(model_.routing_rules().empty());
}

TEST_F(RoutingRuleModelTest, RemovingASpaceRemovesItsRules) {
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetRoutingRule("github.com", work);
  model_.SetRoutingRule("example.com", model_.default_space_id());
  model_.RemoveSpace(work);
  ASSERT_EQ(1u, model_.routing_rules().size());
  EXPECT_EQ("example.com", model_.routing_rules()[0].site);
}

TEST_F(RoutingRuleModelTest, RulesSurviveARoundTrip) {
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetRoutingRule("github.com", work);

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(model_), &restored));
  ASSERT_EQ(1u, restored.routing_rules().size());
  EXPECT_EQ("github.com", restored.routing_rules()[0].site);
  EXPECT_EQ(work, restored.routing_rules()[0].space_id);
}

TEST_F(RoutingRuleModelTest, ARuleNamingASpaceTheFileDoesNotHaveIsDropped) {
  base::DictValue dict = SerializeModel(model_);
  base::DictValue rule;
  rule.Set("site", "github.com");
  rule.Set("space_id", SpaceId::Generate().value());
  base::ListValue rules;
  rules.Append(std::move(rule));
  dict.Set("routing_rules", std::move(rules));

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  EXPECT_TRUE(restored.routing_rules().empty());
}

TEST(RoutingRuleMigrationTest, AVersionFourFileGainsAnEmptyRuleList) {
  base::DictValue dict;
  dict.Set("version", 4);
  dict.Set("spaces", base::ListValue());
  dict.Set("folders", base::ListValue());
  dict.Set("entries", base::ListValue());

  std::optional<base::DictValue> migrated = MigrateModelDict(std::move(dict));

  ASSERT_TRUE(migrated.has_value());
  EXPECT_EQ(5, migrated->FindInt("version"));
  const base::ListValue* rules = migrated->FindList("routing_rules");
  ASSERT_TRUE(rules);
  EXPECT_TRUE(rules->empty());
}

}  // namespace
}  // namespace arcium
