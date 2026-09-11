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
#include "chrome/browser/ui/unload_controller.h"
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

  // Installs a handler that declines closes of `tab` only, so a tab
  // DeleteSpace tries to close stays open exactly as a page behind an
  // unanswered beforeunload dialog would, while any other tab DeleteSpace
  // closes in the same call goes normally. The real dialog cannot run in
  // this fixture (PerformanceManager CHECK); DecliningUnloadHandler stands
  // in for it. The caller must declare a
  // arcium::test::ScopedUnloadHandlerRelease on the return value once it is
  // done asserting, so TearDown can close what is left.
  arcium::test::DecliningUnloadHandler* HoldTabOpen(tabs::TabInterface* tab) {
    auto handler = std::make_unique<arcium::test::DecliningUnloadHandler>();
    handler->set_target(tab->GetContents());
    arcium::test::DecliningUnloadHandler* handler_ptr = handler.get();
    UnloadController::From(browser())->AddTabUnloadHandler(std::move(handler));
    return handler_ptr;
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

TEST_F(SpaceSwitcherTest, DeletingASpaceClosesItsTabsAndTakesItsEntries) {
  const SpaceId first = model_.default_space_id();
  const SpaceId doomed = model_.AddSpace(u"Doomed");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://d1.example/"), doomed);
  model_.AddEntry(doomed, EntryKind::kPinned, GURL("https://d2.example/"),
                  u"D");
  switcher->SwitchTo(doomed);

  switcher->DeleteSpace(doomed);
  EXPECT_FALSE(model_.GetSpace(doomed));
  EXPECT_TRUE(model_.entries().empty());
  ASSERT_EQ(1, strip()->count());
  EXPECT_EQ(first, switcher->SpaceOfTabAt(0));
  // The window moved to the neighbour before the space went, so it is never
  // showing a space that does not exist. With only two spaces, doomed's only
  // neighbour is first, so closing the active doomed tab also leaves
  // Chromium activating a1 -- both paths agree on the same answer here.
  // DeletingTheActiveSpaceMovesToItsNeighbourFirst below uses a third space
  // so the two paths can disagree, which is what isolates the SwitchTo call.
  EXPECT_EQ(first, switcher->active_space());
}

// Three spaces, so a tab held open by an unanswered close lands somewhere
// other than the first space -- SpaceOfTab already reads an unresolvable tag
// as the first space, so a two-space version of this test could not tell a
// real re-tag from that fallback and could not fail if the re-tag loop were
// dropped.
TEST_F(SpaceSwitcherTest, ATabThatSurvivesTheDeleteJoinsTheLandingSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  const SpaceId doomed = model_.AddSpace(u"Doomed");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  tabs::TabInterface* d1 = AddTabInSpace(GURL("https://d1.example/"), doomed);
  switcher->SwitchTo(work);

  arcium::test::DecliningUnloadHandler* handler = HoldTabOpen(d1);
  arcium::test::ScopedUnloadHandlerRelease release(handler);
  switcher->DeleteSpace(doomed);
  ASSERT_EQ(3, strip()->count());  // d1 refused to close.
  EXPECT_EQ(work, switcher->SpaceOfTabAt(strip()->GetIndexOfTab(d1)));
}

// Review finding, Minor 2: DeleteSpace's close loop and its re-tag loop used
// to pick tabs two different ways -- the close loop by SpaceOfTabAt, which
// reads a claimed entry's space first, the re-tag loop by the tab's raw tag.
// A pinned tab whose entry has moved into the doomed space while the tab's
// own tag still names a different, surviving space is picked up by the close
// loop and was missed by the old re-tag loop, so a declined close for it fell
// back to its stale tag's space -- `stale` here -- rather than the space the
// window actually landed on.
TEST_F(SpaceSwitcherTest,
       ASurvivingPinnedTabWithAStaleTagJoinsTheLandingSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId stale = model_.AddSpace(u"Stale");
  const SpaceId doomed = model_.AddSpace(u"Doomed");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  // Tagged `stale`, but its entry (bound below) claims `doomed` -- the
  // mismatch a pin carried into another space would leave behind, since
  // moving a pin retags the entry and not the tab itself.
  tabs::TabInterface* pinned =
      AddTabInSpace(GURL("https://pin.example/"), stale);
  const EntryId entry = model_.AddEntry(doomed, EntryKind::kPinned,
                                        GURL("https://pin.example/"), u"P");
  binding_.Bind(entry, pinned->GetHandle());
  ASSERT_EQ(first, switcher->active_space());
  ASSERT_EQ(doomed, switcher->SpaceOfTabAt(strip()->GetIndexOfTab(pinned)));

  arcium::test::DecliningUnloadHandler* handler = HoldTabOpen(pinned);
  arcium::test::ScopedUnloadHandlerRelease release(handler);
  switcher->DeleteSpace(doomed);
  ASSERT_EQ(2, strip()->count());  // pinned refused to close.
  EXPECT_EQ(first, switcher->SpaceOfTabAt(strip()->GetIndexOfTab(pinned)));
}

TEST_F(SpaceSwitcherTest, TheLastSpaceCannotBeDeleted) {
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  switcher->DeleteSpace(model_.default_space_id());
  EXPECT_EQ(1u, model_.spaces().size());
  EXPECT_EQ(1, strip()->count());
}

TEST_F(SpaceSwitcherTest, OpenTabCountIsWhatTheConfirmationPromises) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://w2.example/"), work);
  EXPECT_EQ(2, switcher->OpenTabCount(work));
}

// The mutation check for DeleteSpace's own SwitchTo(NeighbourOf(id)) call:
// with only two spaces, closing the active doomed tab makes Chromium
// activate the survivor anyway, so dropping that call cannot fail
// DeletingASpaceClosesItsTabsAndTakesItsEntries above. A third space in
// position order after doomed gives SwitchTo(NeighbourOf(id)) a landing that
// Chromium's own activation-on-close would not otherwise reach, and the
// assertion runs before the posted ArciumModel fallback would ever get a
// turn -- DeleteSpace's own SwitchTo is what has to have done it.
TEST_F(SpaceSwitcherTest, DeletingTheActiveSpaceMovesToItsNeighbourFirst) {
  const SpaceId first = model_.default_space_id();
  const SpaceId doomed = model_.AddSpace(u"Doomed");
  const SpaceId third = model_.AddSpace(u"Third");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* d1 = AddTabInSpace(GURL("https://d1.example/"), doomed);
  AddTabInSpace(GURL("https://t1.example/"), third);
  switcher->SwitchTo(doomed);

  arcium::test::DecliningUnloadHandler* handler = HoldTabOpen(d1);
  arcium::test::ScopedUnloadHandlerRelease release(handler);
  switcher->DeleteSpace(doomed);
  EXPECT_EQ(third, switcher->active_space());
  EXPECT_EQ(third, switcher->SpaceOfTabAt(strip()->GetIndexOfTab(d1)));
}

}  // namespace
}  // namespace arcium
