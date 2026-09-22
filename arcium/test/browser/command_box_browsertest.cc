// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The box that Cmd+T, Cmd+L and the address pill all open. It answers while
// you type, a row that is a tab you already have switches to it, and closing
// it leaves the page exactly where it was.

#include <optional>
#include <string>

#include "arcium/test/browser/sidebar_ui_browsertest_base.h"
#include "arcium/ui/browser/box_commands.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/command_box.h"
#include "arcium/ui/browser/command_box_row.h"
#include "arcium/ui/browser/split_controller.h"
#include "base/strings/strcat.h"
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
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/background.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium::test {
namespace {

using CommandBoxTest = SidebarUiTest;

// The three things that decide whether taking "split" asks which tab or
// closes the box, read before Enter: a box that closes anyway then says
// which of them it was. The box closes itself when it loses focus, and a
// suite starting ten browsers at once can take focus from it.
std::string SplitQuestionState(Browser* browser, CommandBox* box) {
  const bool top_is_split = box->row_count_for_testing() > 0 &&
                            box->row_for_testing(0).command_id ==
                                std::optional<int>(kBoxCommandSplit);
  SplitController* split =
      BrowserView::GetBrowserViewForBrowser(browser)->arcium_sidebar()->split();
  TabStripModel* strip = browser->tab_strip_model();
  bool a_partner = false;
  for (int i = 0; i < strip->count(); ++i) {
    a_partner = a_partner || split->CanSplit(strip->active_index(), i);
  }
  return base::StrCat(
      {"top row is split: ", top_is_split ? "yes" : "no",
       "; a partner exists: ", a_partner ? "yes" : "no",
       "; the box has focus: ", box->GetWidget()->IsActive() ? "yes" : "no"});
}

// Put the pointer over the middle of the row at `index`, and optionally
// click there. The events go into the box's own widget rather than through
// the platform, because the box is a second top-level window and a press
// aimed at the browser window never arrives in it on this platform; what is
// being asked is whether the row answers a press that reaches it.
void PointAtRow(CommandBox* box, size_t index, bool click) {
  CommandBoxRow* row = box->row_view_for_testing(index);
  views::Widget* widget = row->GetWidget();
  // A row that has not been laid out yet is a rectangle of no size at the
  // corner, and every click lands somewhere else.
  views::test::RunScheduledLayout(widget);
  gfx::Point point = row->GetBoundsInScreen().CenterPoint();
  views::View::ConvertPointFromScreen(widget->GetRootView(), &point);

  ui::MouseEvent moved(ui::EventType::kMouseMoved, point, point,
                       ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE);
  widget->OnMouseEvent(&moved);
  if (!click) {
    return;
  }
  ui::MouseEvent pressed(ui::EventType::kMousePressed, point, point,
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
  widget->OnMouseEvent(&pressed);
  ui::MouseEvent released(ui::EventType::kMouseReleased, point, point,
                          ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                          ui::EF_LEFT_MOUSE_BUTTON);
  widget->OnMouseEvent(&released);
}

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

// Splitting is the box's one command that has to name something else before
// it can act, so taking it asks rather than closing.
IN_PROC_BROWSER_TEST_F(CommandBoxTest, TakingSplitAsksWhichTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("b.test", "/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);

  OpenBox();
  Type(u"split");
  WaitForRows();
  const std::string before = SplitQuestionState(browser(), Box());
  PressEnter();

  ASSERT_TRUE(Box()) << "the box closed instead of asking which tab; "
                     << before;
  EXPECT_EQ(CommandBox::Mode::kSplitPartner, Box()->mode_for_testing());
  EXPECT_EQ(u"", Box()->text_for_testing());
  ASSERT_GT(Box()->row_count_for_testing(), 0u);
  EXPECT_TRUE(Box()->row_for_testing(0).split_with_tab_index.has_value());
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest,
                       TakingATabInThatQuestionSplitsTheScreen) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("b.test", "/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  const int tabs_before = browser()->tab_strip_model()->count();

  OpenBox();
  Type(u"split");
  WaitForRows();
  const std::string before = SplitQuestionState(browser(), Box());
  PressEnter();
  ASSERT_TRUE(Box()) << before;
  PressEnter();

  EXPECT_FALSE(Box());
  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count())
      << "splitting opened a tab instead of sharing the screen with one";
  EXPECT_EQ(2u, browser()->tab_strip_model()->GetForegroundTabs().size());
}

// The first Escape leaves the question, not the box: a reader who has been
// asked which tab has somewhere to go back to.
IN_PROC_BROWSER_TEST_F(CommandBoxTest, EscapeLeavesTheQuestionBeforeTheBox) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("b.test", "/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);

  OpenBox();
  Type(u"split");
  WaitForRows();
  const std::string before = SplitQuestionState(browser(), Box());
  PressEnter();
  ASSERT_TRUE(Box()) << before;

  PressEscape();
  ASSERT_TRUE(Box()) << "the first Escape closed the box";
  EXPECT_EQ(CommandBox::Mode::kAnything, Box()->mode_for_testing());

  PressEscape();
  EXPECT_FALSE(Box());
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

// The list answers the pointer as well as the keyboard: a row draws itself
// under the pointer, and the row Enter would take keeps its own mark, so the
// reader can see both what moving the mouse away would go back to and what
// clicking here would do.
IN_PROC_BROWSER_TEST_F(CommandBoxTest, ThePointerMarksTheRowItIsOver) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  WaitForHistory(url);

  OpenBox();
  Type(u"a.test");
  WaitForRows();
  ASSERT_GT(Box()->row_count_for_testing(), 1u);
  ASSERT_TRUE(Box()->row_view_for_testing(0)->GetBackground())
      << "the row Enter would take draws nothing";
  ASSERT_FALSE(Box()->row_view_for_testing(1)->GetBackground());

  // Asked of the box each time rather than through kept pointers, and with
  // no run loop in between: answers arrive while the box is open, and every
  // new set throws the row views away and builds them again.
  PointAtRow(Box(), 1, /*click=*/false);

  EXPECT_TRUE(Box()->row_view_for_testing(1)->GetBackground())
      << "nothing happens under the pointer, so a row cannot be found by eye";
  EXPECT_TRUE(Box()->row_view_for_testing(0)->GetBackground());
  EXPECT_EQ(0u, Box()->selected_row_for_testing())
      << "moving the mouse changed what Enter would open";
}

// And a click takes the row it landed on, not the one Enter would have taken.
IN_PROC_BROWSER_TEST_F(CommandBoxTest, AClickTakesTheRowItLandsOn) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL first_url =
      embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL second_url =
      embedded_test_server()->GetURL("a.test", "/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  WaitForHistory(first_url);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url));
  WaitForHistory(second_url);
  // Away again, so that neither page is a tab the reader already has: such a
  // row switches to the tab instead of opening one, and this test counts
  // tabs.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("away.test", "/title3.html")));
  const int tabs_before = browser()->tab_strip_model()->count();

  OpenBox();
  Type(u"a.test");
  WaitForRows();
  ASSERT_GT(Box()->row_count_for_testing(), 1u);
  const GURL wanted = Box()->row_for_testing(1).destination;
  ASSERT_TRUE(wanted.is_valid());
  ASSERT_NE(Box()->row_for_testing(0).destination, wanted)
      << "the two rows go to the same place, so this proves nothing";

  PointAtRow(Box(), 1, /*click=*/true);
  RunLoopUntilIdle();

  EXPECT_FALSE(Box()) << "the click did nothing at all";
  ASSERT_EQ(tabs_before + 1, browser()->tab_strip_model()->count());
  EXPECT_EQ(
      wanted,
      browser()->tab_strip_model()->GetActiveWebContents()->GetVisibleURL())
      << "the click opened the selected row rather than the one under it";
}

}  // namespace
}  // namespace arcium::test
