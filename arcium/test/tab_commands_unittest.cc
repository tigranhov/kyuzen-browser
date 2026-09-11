// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/tab_commands.h"

#include <memory>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/test/bind.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class TabCommandsTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  std::unique_ptr<SpaceSwitcher> MakeSwitcher() {
    return std::make_unique<SpaceSwitcher>(strip(), &model_, &binding_);
  }

  // Appends a tab already tagged with `space`, so the strip ends up in the
  // order the test wrote its calls and every tab is in the space it names.
  tabs::TabInterface* AddTabInSpace(const GURL& url, SpaceId space) {
    return arcium::test::AddTabInSpace(strip(), profile(), url, space);
  }

  GURL UrlAt(int index) {
    return strip()->GetTabAtIndex(index)->GetContents()->GetVisibleURL();
  }

  ArciumModel model_;
  TabBinding binding_;
};

TEST_F(TabCommandsTest, NextAndPreviousStayInTheSpaceAndWrap) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  AddTabInSpace(GURL("https://w2.example/"), work);   // 3
  strip()->ActivateTabAt(0);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_NEXT_TAB));
  EXPECT_EQ(2, strip()->active_index());
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_NEXT_TAB));
  EXPECT_EQ(0, strip()->active_index());
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_PREVIOUS_TAB));
  EXPECT_EQ(2, strip()->active_index());
}

// On a Mac, Ctrl+Tab and Ctrl+Shift+Tab send the cycle commands rather than
// select-next and select-previous, so those have to stay in the space too.
TEST_F(TabCommandsTest, CyclingStaysInTheSpaceAndWraps) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  AddTabInSpace(GURL("https://w2.example/"), work);   // 3
  strip()->ActivateTabAt(0);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_CYCLE_TO_NEXT_TAB));
  EXPECT_EQ(2, strip()->active_index());
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_CYCLE_TO_NEXT_TAB));
  EXPECT_EQ(0, strip()->active_index());
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_CYCLE_TO_PREV_TAB));
  EXPECT_EQ(2, strip()->active_index());
}

TEST_F(TabCommandsTest, CommandDigitsCountTheSpacesOwnTabs) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://w1.example/"), work);   // 0
  AddTabInSpace(GURL("https://a1.example/"), first);  // 1
  AddTabInSpace(GURL("https://w2.example/"), work);   // 2
  switcher->SwitchTo(work);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_TAB_1));
  EXPECT_EQ(2, strip()->active_index());
  strip()->ActivateTabAt(0);
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_LAST_TAB));
  EXPECT_EQ(2, strip()->active_index());
  // A digit past the space's last tab does nothing rather than reaching
  // into another space.
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_TAB_7));
  EXPECT_EQ(2, strip()->active_index());
}

TEST_F(TabCommandsTest, MovingATodayTabSkipsOtherSpacesTabs) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  strip()->ActivateTabAt(0);
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_MOVE_TAB_NEXT));
  // Past the foreign tab, not into its place.
  EXPECT_EQ(2, strip()->active_index());
  EXPECT_EQ(GURL("https://a1.example/"),
            strip()->GetTabAtIndex(2)->GetContents()->GetVisibleURL());
}

TEST_F(TabCommandsTest, MovingATodayTabBackSkipsOtherSpacesTabs) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  strip()->ActivateTabAt(2);
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_MOVE_TAB_PREVIOUS));
  // Past the foreign tab and ahead of a1, not merely into w1's place, which
  // would leave a2 still below a1 in the sidebar.
  EXPECT_EQ(0, strip()->active_index());
  EXPECT_EQ(GURL("https://a2.example/"), UrlAt(0));
  EXPECT_EQ(GURL("https://a1.example/"), UrlAt(1));
  EXPECT_EQ(GURL("https://w1.example/"), UrlAt(2));
}

// The switcher adopts another space's tab the moment it is activated, so a
// window is only still showing one when a command arrives if its switcher did
// not see the activation; the test builds the switcher after it. Either
// direction lands on the space's first open tab in sidebar order -- here the
// pinned tab, which is not the space's first by strip index.
TEST_F(TabCommandsTest, NextAndPreviousFromAnotherSpacesTabLandOnTheFirst) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  tabs::TabInterface* pinned =
      AddTabInSpace(GURL("https://pin.example/"), first);  // 2
  AddTabInSpace(GURL("https://w2.example/"), work);        // 3
  const EntryId pin = model_.AddEntry(first, EntryKind::kPinned,
                                      GURL("https://pin.example/"), u"P");
  binding_.Bind(pin, pinned->GetHandle());

  strip()->ActivateTabAt(1);
  auto switcher = MakeSwitcher();
  ASSERT_EQ(first, switcher->active_space());
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_NEXT_TAB));
  EXPECT_EQ(2, strip()->active_index());

  switcher.reset();
  strip()->ActivateTabAt(3);
  switcher = MakeSwitcher();
  ASSERT_EQ(first, switcher->active_space());
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_PREVIOUS_TAB));
  EXPECT_EQ(2, strip()->active_index());
}

// A pinned or favourite tab's place is its entry's, which the strip does not
// decide: moving it in the strip would change nothing the sidebar draws, and
// closing it along with the Today tabs would take it away from a command
// that Chromium itself never lets reach a pinned tab.
TEST_F(TabCommandsTest, AnEntrysTabIsNeitherMovedNorClosedWithTheTodayTabs) {
  const SpaceId first = model_.default_space_id();
  auto switcher = MakeSwitcher();
  tabs::TabInterface* pinned =
      AddTabInSpace(GURL("https://pin.example/"), first);  // 0
  AddTabInSpace(GURL("https://a1.example/"), first);       // 1
  AddTabInSpace(GURL("https://a2.example/"), first);       // 2
  const EntryId pin = model_.AddEntry(first, EntryKind::kPinned,
                                      GURL("https://pin.example/"), u"P");
  binding_.Bind(pin, pinned->GetHandle());
  strip()->ActivateTabAt(0);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_MOVE_TAB_NEXT));
  EXPECT_EQ(GURL("https://pin.example/"), UrlAt(0));

  strip()->ActivateTabAt(1);
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_WINDOW_CLOSE_OTHER_TABS));
  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(GURL("https://pin.example/"), UrlAt(0));
  EXPECT_EQ(GURL("https://a1.example/"), UrlAt(1));
}

TEST_F(TabCommandsTest, ClosingTheSpacesLastTabLeavesABlankOneBehind) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  switcher->SwitchTo(work);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_CLOSE_TAB));
  EXPECT_EQ(2, strip()->count());
  EXPECT_EQ(work, switcher->SpaceOfTabAt(strip()->active_index()));
  EXPECT_TRUE(
      strip()->GetActiveTab()->GetContents()->GetVisibleURL().is_empty());
}

// The blank tab a close leaves behind is where the quick entry is offered,
// the same as over any other blank tab the switcher opens.
TEST_F(TabCommandsTest, TheBlankTabACloseLeavesRunsTheBlankTabCallback) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  switcher->SwitchTo(work);
  int runs = 0;
  switcher->SetBlankTabCallback(base::BindLambdaForTesting([&] { ++runs; }));

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_CLOSE_TAB));
  EXPECT_EQ(1, runs);
}

class SpaceChangeCounter : public SpaceSwitcher::Observer {
 public:
  void OnActiveSpaceChanged() override { ++changes; }
  int changes = 0;
};

// Whatever the callback shows over the blank tab asks which space the window
// is in, so everything watching the switcher has to have heard of the switch
// by the time it runs -- whether the switch was asked for or came from
// deleting the space on screen.
TEST_F(TabCommandsTest, TheBlankTabCallbackRunsAfterObserversHearOfTheSwitch) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  model_.AddSpace(u"Home");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  SpaceChangeCounter observer;
  switcher->AddObserver(&observer);
  // How many changes the observer had heard each time the callback ran.
  std::vector<int> heard;
  switcher->SetBlankTabCallback(
      base::BindLambdaForTesting([&] { heard.push_back(observer.changes); }));

  switcher->SwitchTo(work);
  ASSERT_EQ(1u, heard.size());
  EXPECT_EQ(1, heard[0]);

  // Work holds only its blank tab, and its neighbour is the empty Home.
  const int before_delete = observer.changes;
  switcher->DeleteSpace(work);
  ASSERT_EQ(2u, heard.size());
  EXPECT_EQ(before_delete + 1, heard[1]);
  switcher->RemoveObserver(&observer);
}

TEST_F(TabCommandsTest, ClosingWhenTheSpaceHasOthersIsChromiumsOwnClose) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://w2.example/"), work);
  switcher->SwitchTo(work);
  EXPECT_FALSE(HandleTabCommand(browser(), IDC_CLOSE_TAB));
  EXPECT_EQ(2, strip()->count());
}

TEST_F(TabCommandsTest, CloseOthersAndCloseToTheRightSpareOtherSpaces) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://w1.example/"), work);   // 0
  AddTabInSpace(GURL("https://a1.example/"), first);  // 1
  AddTabInSpace(GURL("https://w2.example/"), work);   // 2
  AddTabInSpace(GURL("https://w3.example/"), work);   // 3
  switcher->SwitchTo(work);
  strip()->ActivateTabAt(0);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_WINDOW_CLOSE_TABS_TO_RIGHT));
  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(first, switcher->SpaceOfTabAt(1));
}

// A pinned tab keeps whatever strip slot it had when it was pinned, which can
// sit among the Today tabs, while the sidebar draws it above every one of
// them. To the right means below it in the sidebar, so from a pinned tab
// that is every Today tab of the space, whichever side of its slot they are.
TEST_F(TabCommandsTest, CloseToTheRightOfAPinnedTabClosesEveryTodayTab) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  tabs::TabInterface* pinned =
      AddTabInSpace(GURL("https://pin.example/"), first);  // 2
  AddTabInSpace(GURL("https://a2.example/"), first);       // 3
  AddTabInSpace(GURL("https://w2.example/"), work);        // 4
  const EntryId pin = model_.AddEntry(first, EntryKind::kPinned,
                                      GURL("https://pin.example/"), u"P");
  binding_.Bind(pin, pinned->GetHandle());
  strip()->ActivateTabAt(2);
  ASSERT_EQ(first, switcher->active_space());

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_WINDOW_CLOSE_TABS_TO_RIGHT));
  ASSERT_EQ(3, strip()->count());
  EXPECT_EQ(GURL("https://w1.example/"), UrlAt(0));
  EXPECT_EQ(GURL("https://pin.example/"), UrlAt(1));
  EXPECT_EQ(GURL("https://w2.example/"), UrlAt(2));
  EXPECT_EQ(1, strip()->active_index());
}

// Close-others reaches both sides of the active tab, where close-to-the-right
// stops at it; both leave another space's tab alone.
TEST_F(TabCommandsTest, CloseOthersReachesBothSidesAndSparesOtherSpaces) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://w1.example/"), work);   // 0
  AddTabInSpace(GURL("https://a1.example/"), first);  // 1
  AddTabInSpace(GURL("https://w2.example/"), work);   // 2
  AddTabInSpace(GURL("https://w3.example/"), work);   // 3
  switcher->SwitchTo(work);
  strip()->ActivateTabAt(2);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_WINDOW_CLOSE_OTHER_TABS));
  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(GURL("https://a1.example/"), UrlAt(0));
  EXPECT_EQ(GURL("https://w2.example/"), UrlAt(1));
  EXPECT_EQ(1, strip()->active_index());
}

TEST_F(TabCommandsTest, AnUnownedCommandIsRefused) {
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  EXPECT_FALSE(HandleTabCommand(browser(), IDC_RELOAD));
}

// A window with no sidebar has no switcher, and every command is Chromium's.
TEST_F(TabCommandsTest, WithoutASwitcherEveryCommandIsRefused) {
  AddTab(browser(), GURL("https://a1.example/"));
  EXPECT_FALSE(HandleTabCommand(browser(), IDC_SELECT_NEXT_TAB));
}

// Through Chromium's own command path rather than HandleTabCommand directly,
// so a hook missing from the command controller, or wired to the wrong case,
// fails here and not only by hand in the browser. The strip is built so that
// Chromium's own answer leaves the space whether Ctrl+Tab walks the strip or
// the most-recently-used order: w1 is both the next tab and the last one
// shown.
TEST_F(TabCommandsTest, ChromiumsOwnCommandPathStaysInTheSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  strip()->ActivateTabAt(1);
  strip()->ActivateTabAt(0);
  ASSERT_EQ(first, switcher->active_space());

  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_CYCLE_TO_NEXT_TAB));
  EXPECT_EQ(2, strip()->active_index());
  EXPECT_EQ(first, switcher->active_space());

  strip()->ActivateTabAt(0);
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_SELECT_TAB_1));
  EXPECT_EQ(2, strip()->active_index());
  EXPECT_EQ(first, switcher->active_space());
}

}  // namespace
}  // namespace arcium
