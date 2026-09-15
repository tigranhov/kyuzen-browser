// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A link another application hands the running browser opens in a small
// window of its own (R4.3), in the space a routing rule names. The tests call
// arcium::OpenOutsideLinks directly, which is exactly what the macOS hook in
// patch 0220 does with the URLs AppKit gives it.

#include <vector>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/loose_page.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/tab_space.h"
#include "arcium/test/browser/sidebar_ui_browsertest_base.h"
#include "arcium/ui/browser/outside_link_window.h"
#include "arcium/ui/browser/outside_links.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium::test {
namespace {

class OutsideLinksTest : public SidebarUiTest {
 protected:
  ArciumProfileState* State() {
    return ArciumProfileState::GetForBrowserContext(browser()->GetProfile());
  }

  SpaceId ActiveSpace() {
    return SpaceSwitcher::FromTabStripModel(browser()->tab_strip_model())
        ->active_space();
  }

  void CloseEveryWindow() {
    while (OutsideLinkWindow* window = OutsideLinkWindow::LastForTesting()) {
      window->Close();
      RunLoopUntilIdle();
    }
  }
};

IN_PROC_BROWSER_TEST_F(OutsideLinksTest, ALinkOpensASmallWindowInTheRuleSpace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("work.test", "/title1.html");
  const SpaceId work = State()->model()->AddSpace(u"Work");
  State()->model()->SetRoutingRule("work.test", work);
  ASSERT_NE(work, ActiveSpace());
  const int tabs_before = browser()->tab_strip_model()->count();

  ASSERT_TRUE(OpenOutsideLinks({url}));
  RunLoopUntilIdle();

  ASSERT_EQ(1u, OutsideLinkWindow::CountForTesting());
  content::WebContents* page =
      OutsideLinkWindow::LastForTesting()->page_for_testing();
  ASSERT_TRUE(page);
  // The page is a tab in the main window that the sidebar does not draw, and
  // it is in the rule's space, not the one on screen.
  EXPECT_EQ(tabs_before + 1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(IsLoosePage(page));
  EXPECT_EQ(work, SpaceTagOf(page));
  EXPECT_NE(page, browser()->tab_strip_model()->GetActiveWebContents());

  CloseEveryWindow();
  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count())
      << "closing the window left its page open";
}

IN_PROC_BROWSER_TEST_F(OutsideLinksTest, OpenInSpaceMovesThePageIntoTheWindow) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("work.test", "/title1.html");
  const SpaceId work = State()->model()->AddSpace(u"Work");
  State()->model()->SetRoutingRule("work.test", work);

  ASSERT_TRUE(OpenOutsideLinks({url}));
  RunLoopUntilIdle();
  ASSERT_EQ(1u, OutsideLinkWindow::CountForTesting());
  content::WebContents* page =
      OutsideLinkWindow::LastForTesting()->page_for_testing();

  OutsideLinkWindow::LastForTesting()->OpenInSpace();
  RunLoopUntilIdle();

  EXPECT_EQ(0u, OutsideLinkWindow::CountForTesting());
  EXPECT_FALSE(IsLoosePage(page));
  EXPECT_EQ(page, browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(work, ActiveSpace()) << "the window did not follow the page";
}

IN_PROC_BROWSER_TEST_F(OutsideLinksTest, WithNoRuleTheSpaceOnScreenTakesIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");

  ASSERT_TRUE(OpenOutsideLinks({url}));
  RunLoopUntilIdle();

  ASSERT_EQ(1u, OutsideLinkWindow::CountForTesting());
  EXPECT_EQ(
      ActiveSpace(),
      SpaceTagOf(OutsideLinkWindow::LastForTesting()->page_for_testing()));
  CloseEveryWindow();
}

IN_PROC_BROWSER_TEST_F(OutsideLinksTest, AnythingThatIsNotAWebPageIsDeclined) {
  // Declined, and nothing opened: the caller -- Chromium's own openURLs path
  // -- is what opens these, as a tab.
  EXPECT_FALSE(OpenOutsideLinks({GURL("file:///tmp/a.html")}));
  EXPECT_FALSE(OpenOutsideLinks({GURL("chrome://settings")}));
  EXPECT_FALSE(OpenOutsideLinks({}));
  EXPECT_EQ(0u, OutsideLinkWindow::CountForTesting());
}

IN_PROC_BROWSER_TEST_F(OutsideLinksTest, TwoLinksAreTwoWindows) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(OpenOutsideLinks(
      {embedded_test_server()->GetURL("a.test", "/title1.html"),
       embedded_test_server()->GetURL("b.test", "/title2.html")}));
  RunLoopUntilIdle();

  EXPECT_EQ(2u, OutsideLinkWindow::CountForTesting());
  CloseEveryWindow();
  EXPECT_EQ(0u, OutsideLinkWindow::CountForTesting());
}

}  // namespace
}  // namespace arcium::test
