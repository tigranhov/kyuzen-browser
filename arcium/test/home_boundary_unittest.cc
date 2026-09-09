// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/home_boundary.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// Functions rather than namespace-scope objects: a GURL at file scope needs
// an exit-time destructor, which this tree builds with
// -Wexit-time-destructors.
GURL Home() {
  return GURL("https://mail.google.com/mail/u/0");
}
GURL Accounts() {
  return GURL("https://accounts.google.com/signin");
}
GURL News() {
  return GURL("https://news.ycombinator.com/");
}

TEST(HomeBoundaryTest, ADifferentHostLeaves) {
  EXPECT_TRUE(LinkLeavesHome(Home(), News(), Home()));
}

TEST(HomeBoundaryTest, TheSameHostStays) {
  EXPECT_FALSE(
      LinkLeavesHome(Home(), GURL("https://mail.google.com/mail/u/1"), Home()));
}

TEST(HomeBoundaryTest, ADifferentPortOrSchemeOnTheSameHostStays) {
  // Firefox compares hosts and consults neither scheme nor port. Matching it
  // means an http link from an https page is not "leaving".
  EXPECT_FALSE(
      LinkLeavesHome(Home(), GURL("http://mail.google.com:8080/x"), Home()));
}

TEST(HomeBoundaryTest, AWwwPrefixIsNotADifferentHost) {
  EXPECT_FALSE(LinkLeavesHome(GURL("https://example.com/a"),
                              GURL("https://www.example.com/b"),
                              GURL("https://example.com/a")));
}

TEST(HomeBoundaryTest, ADroppedWwwPrefixIsNotADifferentHostEither) {
  // The allowance runs in both directions, which is why one helper does both
  // comparisons.
  EXPECT_FALSE(LinkLeavesHome(GURL("https://www.example.com/a"),
                              GURL("https://example.com/b"),
                              GURL("https://www.example.com/a")));
}

TEST(HomeBoundaryTest, AHostThatMerelyEndsInTheHomeHostLeaves) {
  // "notexample.com" must not pass as "example.com". The comparison is host
  // equality, not a suffix test.
  EXPECT_TRUE(LinkLeavesHome(GURL("https://example.com/a"),
                             GURL("https://notexample.com/b"),
                             GURL("https://example.com/a")));
}

TEST(HomeBoundaryTest, ASubdomainLeaves) {
  // Confirmed against Zen by hand: docs.github.com from a pinned github.com
  // is diverted. The rule is host equality, not registrable domain.
  EXPECT_TRUE(LinkLeavesHome(GURL("https://github.com/foo"),
                             GURL("https://docs.github.com/bar"),
                             GURL("https://github.com/foo")));
}

TEST(HomeBoundaryTest, HomeMovesWithYou) {
  // Once the tab has walked to another host, the boundary is measured from
  // there. A link back to where you already are stays.
  EXPECT_FALSE(LinkLeavesHome(
      Accounts(), GURL("https://accounts.google.com/consent"), Home()));
}

// --- Condition 6: the deliberate divergence from Zen. -----------------------
// Deleting this clause and the two tests below restores Firefox's rule
// exactly. They are named so that is a mechanical deletion.

TEST(HomeBoundaryTest, DivergenceALinkBackToTheStoredHomeHostStays) {
  // The case a host-only rule handles badly: a pinned mail.google.com that
  // has walked to accounts.google.com, where clicking back to mail would be
  // torn out of its own pinned tab.
  EXPECT_FALSE(LinkLeavesHome(Accounts(), Home(), Home()));
}

TEST(HomeBoundaryTest, DivergenceTheStoredHomeHostAlsoGetsTheWwwAllowance) {
  EXPECT_FALSE(LinkLeavesHome(GURL("https://accounts.google.com/signin"),
                              GURL("https://www.example.com/x"),
                              GURL("https://example.com/home")));
}

// --- Guards. ----------------------------------------------------------------

TEST(HomeBoundaryTest, ANonHttpSchemeStays) {
  // mailto:, tel: and custom protocols never divert; they fall through to
  // Chromium's external-protocol handling exactly as they do today.
  EXPECT_FALSE(
      LinkLeavesHome(Home(), GURL("mailto:someone@example.com"), Home()));

  // mailto: has no host, so the assertion above is also caught by the
  // has-host guard and does not alone prove the scheme guard exists. ftp: is
  // a "standard" scheme with a host in GURL's parser, on a host that differs
  // from both current and home, so only the scheme guard can be holding this
  // one in the tab.
  EXPECT_FALSE(
      LinkLeavesHome(Home(), GURL("ftp://files.example.com/x"), Home()));
}

TEST(HomeBoundaryTest, AboutBlankStays) {
  EXPECT_FALSE(LinkLeavesHome(Home(), GURL("about:blank"), Home()));
}

TEST(HomeBoundaryTest, AnInvalidTargetStays) {
  EXPECT_FALSE(LinkLeavesHome(Home(), GURL(), Home()));
}

TEST(HomeBoundaryTest, AnEmptyStoredHomeSimplyNeverMatches) {
  // An entry with no stored URL must not accidentally match everything.
  EXPECT_TRUE(LinkLeavesHome(Home(), News(), GURL()));
}

TEST(HomeBoundaryTest, ATabWithNoCommittedUrlFallsBackToTheStoredHome) {
  // A fresh or failed-load tab has no current host, so only condition 6 can
  // hold the click.
  EXPECT_FALSE(LinkLeavesHome(GURL(), Home(), Home()));
  EXPECT_TRUE(LinkLeavesHome(GURL(), News(), Home()));
}

}  // namespace

}  // namespace arcium
