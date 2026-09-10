// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/space_switcher.h"

#include <memory>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class SpaceSwitcherTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  std::unique_ptr<SpaceSwitcher> MakeSwitcher() {
    return std::make_unique<SpaceSwitcher>(strip(), &model_, &binding_);
  }

  // Appends a tab already tagged with `space`, mirroring how a restored tab
  // arrives. BrowserWithTestWindowTest::AddTab inserts at index 0 in the
  // foreground, which would both reverse call order and tag a tab only after
  // it landed -- too late for SpaceSwitcher's never-overwrite rule to see
  // anything but the active space. This appends instead and tags first.
  tabs::TabInterface* AddTabInSpace(const GURL& url, SpaceId space) {
    return arcium::test::AddTabInSpace(strip(), profile(), url, space);
  }

  // What a Cmd+click does: a tab whose strip opener is `opener`, inserted
  // while some other tab may be active.
  tabs::TabInterface* AddTabWithOpener(tabs::TabInterface* opener) {
    return arcium::test::AddTabWithOpener(
        strip(), profile(), GURL("https://opened.example/"), opener);
  }

  ArciumModel model_;
  TabBinding binding_;
};

TEST_F(SpaceSwitcherTest, TheWindowOpensOnTheSpaceThatWasActiveAtQuit) {
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetLastActiveSpace(work);
  auto switcher = MakeSwitcher();
  EXPECT_EQ(work, switcher->active_space());
}

TEST_F(SpaceSwitcherTest, ANewTabJoinsTheActiveSpace) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  switcher->SwitchTo(work);
  AddTab(browser(), GURL("https://a.example/"));
  EXPECT_EQ(work, switcher->SpaceOfTabAt(0));
}

// The controller's finding on this branch: TabStripModel sets a new tab's
// opener to the active tab automatically for a foreground link-style insert,
// so this cannot be built by adding a tab in the foreground after switching
// -- SwitchTo has already made a `work` tab active by then. AddTabWithOpener
// instead builds a tab whose opener was set some other way (ADD_FORCE_INDEX
// keeps TabStripModel from overwriting it), which is the one real path where
// the opener and the active tab differ.
TEST_F(SpaceSwitcherTest, ANewTabJoinsItsOpenersSpaceNotTheActiveOne) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  tabs::TabInterface* opener =
      AddTabInSpace(GURL("https://opener.example/"), first);
  switcher->SwitchTo(work);
  // What a Cmd+click does: an inserted tab whose strip opener is the tab it
  // came from, while another space is on screen.
  tabs::TabInterface* new_tab = AddTabWithOpener(opener);
  EXPECT_EQ(first, switcher->SpaceOfTabAt(strip()->GetIndexOfTab(new_tab)));
}

TEST_F(SpaceSwitcherTest, SwitchingRecordsWhereYouWereAndLandsWhereYouLeft) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  // Interleaved, so a path that ignores spaces lands in the wrong one
  // rather than passing by luck.
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://a2.example/"), first);
  AddTabInSpace(GURL("https://w2.example/"), work);
  strip()->ActivateTabAt(2);

  switcher->SwitchTo(work);
  EXPECT_EQ(work, switcher->active_space());
  EXPECT_EQ(work, switcher->SpaceOfTabAt(strip()->active_index()));
  EXPECT_EQ(KeyOf(strip()->GetTabAtIndex(2)->GetContents()),
            model_.GetSpace(first)->last_active_tab);

  strip()->ActivateTabAt(3);
  switcher->SwitchTo(first);
  switcher->SwitchTo(work);
  EXPECT_EQ(3, strip()->active_index());
}

// Isolates SwitchTo's own RecordActiveTab call from the strip observer's:
// the tab here is appended (and auto-activated, being the only tab) before
// the switcher exists, so no OnTabStripModelChanged notification for its
// activation was ever seen. Only SwitchTo's own call can record it, which is
// what the mutation check in the stage-3a ledger asks for -- the sibling
// test above cannot show it, because every activation there flows through
// strip()->ActivateTabAt while the switcher is already observing, and the
// observer's own recording (ruling A2) satisfies that test's assertions
// whether or not SwitchTo records anything itself.
TEST_F(SpaceSwitcherTest, SwitchingRecordsATabTheObserverNeverSawActivated) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  auto switcher = MakeSwitcher();

  switcher->SwitchTo(work);

  EXPECT_EQ(KeyOf(strip()->GetTabAtIndex(0)->GetContents()),
            model_.GetSpace(first)->last_active_tab);
}

TEST_F(SpaceSwitcherTest, ActivatingAnotherSpacesTabSwitchesToIt) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  ASSERT_EQ(first, switcher->active_space());
  strip()->ActivateTabAt(1);
  EXPECT_EQ(work, switcher->active_space());
  // And it did not move the selection it was told about.
  EXPECT_EQ(1, strip()->active_index());
}

TEST_F(SpaceSwitcherTest, SwitchingToAnEmptySpaceOpensOneBlankTab) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  switcher->SwitchTo(work);
  EXPECT_EQ(2, strip()->count());
  EXPECT_EQ(work, switcher->SpaceOfTabAt(strip()->active_index()));
  EXPECT_TRUE(
      strip()->GetActiveTab()->GetContents()->GetVisibleURL().is_empty());
}

TEST_F(SpaceSwitcherTest,
       SidebarOrderIsFavouritesThenPinsThenTodayInStripOrder) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://today1.example/"), work);
  AddTabInSpace(GURL("https://pin.example/"), work);
  AddTabInSpace(GURL("https://fav.example/"), work);
  AddTabInSpace(GURL("https://today2.example/"), work);
  const EntryId pin = model_.AddEntry(work, EntryKind::kPinned,
                                      GURL("https://pin.example/"), u"P");
  const EntryId fav = model_.AddEntry(work, EntryKind::kFavorite,
                                      GURL("https://fav.example/"), u"F");
  binding_.Bind(pin, strip()->GetTabAtIndex(1)->GetHandle());
  binding_.Bind(fav, strip()->GetTabAtIndex(2)->GetHandle());
  EXPECT_EQ(std::vector<int>({2, 1, 0, 3}),
            switcher->OpenTabsInSidebarOrder(work));
}

TEST_F(SpaceSwitcherTest, MovingATabToAnotherSpaceRetagsItAndFollowsIt) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  strip()->ActivateTabAt(0);
  switcher->MoveTabToSpace(0, work);
  EXPECT_EQ(work, switcher->SpaceOfTabAt(0));
  // Moving the tab you are looking at takes you with it, as Zen does.
  EXPECT_EQ(work, switcher->active_space());
}

// Review finding, Important 2: MoveTabToSpace used to call SwitchTo on the
// target, which lands on whatever that space already remembers as its last
// active tab -- a different tab than the one that just moved, so the page
// the user was looking at would disappear behind it. Moving the active tab
// must adopt the target space instead: land on the moved tab itself, and
// record it as the target's new place.
TEST_F(SpaceSwitcherTest,
       MovingTheActiveTabFollowsItPastTheTargetsRecordedTab) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  tabs::TabInterface* a1 = AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* w1 = AddTabInSpace(GURL("https://w1.example/"), work);

  // Work already remembers w1 as where the user left it.
  strip()->ActivateTabAt(strip()->GetIndexOfTab(w1));
  ASSERT_EQ(work, switcher->active_space());
  strip()->ActivateTabAt(strip()->GetIndexOfTab(a1));
  ASSERT_EQ(first, switcher->active_space());
  ASSERT_EQ(KeyOf(w1->GetContents()), model_.GetSpace(work)->last_active_tab);

  // Moving the tab the user is looking at into work should take them with
  // it, landing on the moved tab -- not on w1, which work still remembers.
  switcher->MoveTabToSpace(strip()->GetIndexOfTab(a1), work);

  EXPECT_EQ(work, switcher->active_space());
  EXPECT_EQ(strip()->GetIndexOfTab(a1), strip()->active_index());
  EXPECT_EQ(KeyOf(a1->GetContents()), model_.GetSpace(work)->last_active_tab);
}

// Review finding, Important 1 (chained adoption): activating another
// space's tab adopts that space without recording which tab was adopted, so
// if the window then adopts a third space before the user ever switches
// back, the second space's remembered tab is stale -- SwitchTo lands on
// whatever was recorded before the adoption, not on the tab the user
// actually left it on.
TEST_F(SpaceSwitcherTest, ChainedAdoptionKeepsEachSpacesPlace) {
  const SpaceId a = model_.default_space_id();
  const SpaceId b = model_.AddSpace(u"B");
  const SpaceId c = model_.AddSpace(u"C");
  auto switcher = MakeSwitcher();
  tabs::TabInterface* a1 = AddTabInSpace(GURL("https://a1.example/"), a);
  tabs::TabInterface* b0 = AddTabInSpace(GURL("https://b0.example/"), b);
  tabs::TabInterface* b1 = AddTabInSpace(GURL("https://b1.example/"), b);
  tabs::TabInterface* c1 = AddTabInSpace(GURL("https://c1.example/"), c);

  // The user is on a1 in space A, with b0 recorded as B's last active tab.
  strip()->ActivateTabAt(strip()->GetIndexOfTab(b0));
  strip()->ActivateTabAt(strip()->GetIndexOfTab(a1));
  ASSERT_EQ(a, switcher->active_space());
  ASSERT_EQ(KeyOf(b0->GetContents()), model_.GetSpace(b)->last_active_tab);

  // Ctrl+Tab reaches b1, so the window adopts B.
  strip()->ActivateTabAt(strip()->GetIndexOfTab(b1));
  ASSERT_EQ(b, switcher->active_space());

  // Ctrl+Tab reaches c1, so the window adopts C.
  strip()->ActivateTabAt(strip()->GetIndexOfTab(c1));
  ASSERT_EQ(c, switcher->active_space());

  // Clicking B's dot should land on b1, where the user left it -- not b0.
  switcher->SwitchTo(b);
  EXPECT_EQ(strip()->GetIndexOfTab(b1), strip()->active_index());
}

// Review finding, Important 1 (stale landing): SwitchTo's own landing
// activation used to be suppressed by `switching_`, so the tab a switch
// lands on -- an existing open tab or a freshly opened blank one -- was
// never recorded as the space's last active tab.
TEST_F(SpaceSwitcherTest, SwitchingRecordsTheTabItLandsOn) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* w1 = AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://w2.example/"), work);

  switcher->SwitchTo(work);

  EXPECT_EQ(strip()->GetIndexOfTab(w1), strip()->active_index());
  EXPECT_EQ(KeyOf(w1->GetContents()), model_.GetSpace(work)->last_active_tab);
}

TEST_F(SpaceSwitcherTest, SwitchingIntoAnEmptySpaceRecordsTheBlankTab) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);

  switcher->SwitchTo(work);

  EXPECT_EQ(KeyOf(strip()->GetActiveTab()->GetContents()),
            model_.GetSpace(work)->last_active_tab);
}

// Ruling A4 in the stage-3a ledger: the fallback in OnArciumModelChanged is
// posted rather than run inline, because RecordActiveTab can mutate the
// model from inside a strip observer callback and a synchronous SwitchTo
// there would re-enter the strip while its own ReentrancyCheck is held.
// Right after the space vanishes the switcher still names it -- the posted
// task has not run yet -- and only settles on last_active_space() once the
// run loop is drained.
TEST_F(SpaceSwitcherTest, RemovingTheActiveSpaceFallsBackAfterThePostedTask) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  switcher->SwitchTo(work);
  ASSERT_EQ(work, switcher->active_space());

  model_.RemoveSpace(work);
  EXPECT_EQ(work, switcher->active_space());

  task_environment()->RunUntilIdle();
  EXPECT_EQ(first, switcher->active_space());
}

}  // namespace
}  // namespace arcium
