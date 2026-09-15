// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The box that Cmd+T, Cmd+L and the address pill all open. It answers while
// you type, a row that is a tab you already have switches to it, and closing
// it leaves the page exactly where it was.

#include <optional>

#include "arcium/test/browser/sidebar_ui_browsertest_base.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/command_box.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace arcium::test {
namespace {

using CommandBoxTest = SidebarUiTest;

IN_PROC_BROWSER_TEST_F(CommandBoxTest, TypingGetsAnswers) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  WaitForHistory(url);

  OpenBox();
  Type(u"a.test");
  WaitForRows();
  EXPECT_GT(Box()->row_count_for_testing(), 0u);
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, EnterOnATabYouHaveSwitchesToIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  // A second tab, so that the first one is still on a.test when the box is
  // asked about it. Navigating the same tab twice would leave a.test in
  // history and in no tab at all, and then this test would be waiting for a
  // row that cannot arrive.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("b.test", "/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  const int tabs_before = browser()->tab_strip_model()->count();
  ASSERT_EQ(2, tabs_before);

  OpenBox();
  Type(u"a.test");
  WaitForRowThatIsAnOpenTab();
  PressEnter();

  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count())
      << "a tab we already had was opened a second time";
  EXPECT_EQ(url, browser()
                     ->tab_strip_model()
                     ->GetActiveWebContents()
                     ->GetLastCommittedURL());

  // The control. Everything above says a tab was not added, which is also
  // what a box wired to nothing would report. A row that is not an open tab
  // must add one.
  OpenBox();
  Type(base::UTF8ToUTF16(
      embedded_test_server()->GetURL("c.test", "/title3.html").spec()));
  WaitForRows();
  PressEnter();
  EXPECT_EQ(tabs_before + 1, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, EscapeLeavesThePageAlone) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  OpenBox();
  Type(u"b.test");
  PressEscape();
  EXPECT_FALSE(Box());
  EXPECT_EQ(url, browser()
                     ->tab_strip_model()
                     ->GetActiveWebContents()
                     ->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, ClickingThePillOpensIt) {
  ASSERT_FALSE(Box());
  // A click is delivered to the window under the pointer, so a window that
  // another test's window has taken the front from never sees it. Alone this
  // test passed and beside others it did not, which is that and nothing
  // about the pill.
  browser()->GetWindow()->Activate();
  ui_test_utils::WaitForBrowserSetLastActive(browser());
  ClickPillBackground();
  EXPECT_TRUE(Box());
  // And the address bar behind the pill did not take focus: one box, one
  // place suggestions come from.
  EXPECT_FALSE(BrowserView::GetBrowserViewForBrowser(browser())
                   ->GetLocationBarView()
                   ->HasFocus());
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, NothingIsRunningWhileItIsClosed) {
  // The suggestion machinery is built with the box and dies with it.
  EXPECT_FALSE(Controller()->suggestion_source_for_testing());
  OpenBox();
  EXPECT_TRUE(Controller()->suggestion_source_for_testing());
  PressEscape();
  EXPECT_FALSE(Controller()->suggestion_source_for_testing());
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, APinnedPageIsOfferedBeforeAHistoryHit) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL pinned =
      embedded_test_server()->GetURL("pin.test", "/title1.html");
  PinEntryWithUrl(pinned, u"A pinned page");
  const GURL visited =
      embedded_test_server()->GetURL("pin.test", "/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), visited));
  WaitForHistory(visited);
  // And then away again. Visiting a page is how history gets written, but it
  // also leaves a tab sitting on it, and a tab the reader already has is
  // offered before either of the two things this test is comparing -- so
  // without this the test asks its question of the wrong pair.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("away.test", "/title3.html")));

  OpenBox();
  Type(u"pin.test");
  WaitForRows();
  ASSERT_GT(Box()->row_count_for_testing(), 0u);
  EXPECT_EQ(pinned, Box()->row_for_testing(0).destination);
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest,
                       FocusLocationOpensTheBoxHoldingTheAddress) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  chrome::ExecuteCommand(browser(), IDC_FOCUS_LOCATION);
  ASSERT_TRUE(Box());
  EXPECT_EQ(base::UTF8ToUTF16(url.spec()), Box()->text_for_testing());
  // Selected end to end, so typing replaces it, which is what this key is
  // for.
  EXPECT_EQ(url.spec().size(), Box()->selected_length_for_testing());

  // The bar behind the pill did not take the focus instead.
  EXPECT_FALSE(BrowserView::GetBrowserViewForBrowser(browser())
                   ->GetLocationBarView()
                   ->HasFocus());
}

}  // namespace
}  // namespace arcium::test
