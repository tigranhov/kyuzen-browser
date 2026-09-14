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
#include "chrome/browser/ui/browser.h"
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
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("b.test", "/title2.html")));
  const int tabs_before = browser()->tab_strip_model()->count();

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

}  // namespace
}  // namespace arcium::test
