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
