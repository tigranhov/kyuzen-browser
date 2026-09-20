// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A link that leaves a pinned entry's home opens in a peek over that entry
// (R4.4): a real tab the sidebar does not draw, shown on a card over the
// page. What these tests are about is that the pinned tab is left exactly
// where it was, that the sidebar gains no row, and that open-as-tab turns the
// peek into an ordinary Today tab in the pin's space.

#include <optional>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/loose_page.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/test/browser/sidebar_ui_browsertest_base.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/peek_controller.h"
#include "arcium/ui/browser/peek_view.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"
#include "ui/compositor/layer.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/view.h"
#include "url/gurl.h"

namespace arcium::test {
namespace {

class PeekTest : public SidebarUiTest {
 protected:
  ArciumProfileState* State() {
    return ArciumProfileState::GetForBrowserContext(browser()->GetProfile());
  }

  // Makes the tab on screen a pinned entry whose home is where it is now,
  // which is the one state the home boundary is about.
  void PinTheTabOnScreen(const GURL& home) {
    const EntryId id =
        State()->model()->AddEntryForTesting(EntryKind::kPinned, home, u"Home");
    State()->binding()->Bind(
        id, browser()->tab_strip_model()->GetTabAtIndex(0)->GetHandle());
    RunLoopUntilIdle();
  }

  // A click on a real link, which is the only thing the boundary answers to:
  // ExecJs carries a user gesture, and Blink classifies an anchor's click as
  // a link click.
  void ClickLinkTo(const GURL& url) {
    content::WebContents* contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(content::ExecJs(
        contents, content::JsReplace("const a = document.createElement('a');"
                                     "a.href = $1;"
                                     "a.textContent = 'go';"
                                     "document.body.appendChild(a);"
                                     "a.click();",
                                     url)));
    RunLoopUntilIdle();
  }

  PeekController* Peek() { return Controller()->peek(); }
};

IN_PROC_BROWSER_TEST_F(PeekTest, AnOffHomeLinkInAPinnedTabOpensAPeek) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL home = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL elsewhere =
      embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), home));
  PinTheTabOnScreen(home);
  const size_t rows_before = Controller()->model_for_testing()->rows().size();

  ClickLinkTo(elsewhere);

  ASSERT_TRUE(Peek());
  EXPECT_TRUE(Peek()->is_showing());
  // The pinned tab is where it was, and still on its home.
  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());
  EXPECT_EQ(home, browser()
                      ->tab_strip_model()
                      ->GetActiveWebContents()
                      ->GetLastCommittedURL());
  // The page is a tab, because only a tab has the password manager and the
  // extensions -- but a tab the sidebar does not draw.
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_TRUE(IsLoosePage(Peek()->page_for_testing()));
  EXPECT_EQ(rows_before, Controller()->model_for_testing()->rows().size());
}

IN_PROC_BROWSER_TEST_F(PeekTest, ClosingThePeekClosesItsPage) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL home = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), home));
  PinTheTabOnScreen(home);
  ClickLinkTo(embedded_test_server()->GetURL("b.test", "/title2.html"));
  ASSERT_TRUE(Peek()->is_showing());

  Peek()->Close();
  RunLoopUntilIdle();

  EXPECT_FALSE(Peek()->is_showing());
  EXPECT_EQ(1, browser()->tab_strip_model()->count())
      << "the peek's page was left open behind it";
}

IN_PROC_BROWSER_TEST_F(PeekTest, OpenAsTabPromotesThePageIntoThePinsSpace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL home = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL elsewhere =
      embedded_test_server()->GetURL("b.test", "/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), home));
  PinTheTabOnScreen(home);
  ClickLinkTo(elsewhere);
  ASSERT_TRUE(Peek()->is_showing());
  content::WebContents* page = Peek()->page_for_testing();
  const SpaceId space =
      SpaceSwitcher::FromTabStripModel(browser()->tab_strip_model())
          ->active_space();

  Peek()->OpenAsTab();
  RunLoopUntilIdle();

  EXPECT_FALSE(Peek()->is_showing());
  EXPECT_FALSE(IsLoosePage(page));
  EXPECT_EQ(page, browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(page);
  ASSERT_TRUE(tab);
  EXPECT_EQ(space, SpaceOfTab(*State()->model(), *State()->binding(),
                              tab->GetHandle()));
}

IN_PROC_BROWSER_TEST_F(PeekTest, GoingToAnotherTabPutsThePeekAway) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL home = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), home));
  PinTheTabOnScreen(home);
  // A second tab to go to, beside the pinned one.
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("c.test", "/title3.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  // Back to the pinned tab, and off its home from there.
  browser()->tab_strip_model()->ActivateTabAt(0);
  RunLoopUntilIdle();
  ClickLinkTo(embedded_test_server()->GetURL("b.test", "/title2.html"));
  ASSERT_TRUE(Peek()->is_showing());
  const int tabs_with_peek = browser()->tab_strip_model()->count();

  browser()->tab_strip_model()->ActivateTabAt(1);
  RunLoopUntilIdle();

  EXPECT_FALSE(Peek()->is_showing());
  EXPECT_EQ(tabs_with_peek - 1, browser()->tab_strip_model()->count());
}

// The scrim and the two buttons are the peek's own drawing, and the page
// beside them draws through a compositor layer of its own. A view without a
// layer paints into its parent's, which is under every layer in it: so
// without one of its own the card shows and everything the peek draws around
// it does not -- no dimming, and no buttons, on a surface whose only other
// way out is a key.
IN_PROC_BROWSER_TEST_F(PeekTest, ThePeekDrawsOverThePageBesideIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL home = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), home));
  PinTheTabOnScreen(home);
  ClickLinkTo(embedded_test_server()->GetURL("b.test", "/title2.html"));
  ASSERT_TRUE(Peek()->is_showing());

  PeekView* view = Peek()->view_for_testing();
  ASSERT_TRUE(view);
  ASSERT_TRUE(view->layer())
      << "the scrim and the buttons would paint under the page";

  ui::Layer* parent = view->layer()->parent();
  ASSERT_TRUE(parent);
  EXPECT_EQ(parent->children().back(), view->layer())
      << "something else in the window draws over the peek";

  // And the buttons are where a reader can reach them: on screen, beside the
  // card rather than under it.
  EXPECT_TRUE(view->close_button_for_testing()->GetVisible());
  EXPECT_TRUE(view->open_button_for_testing()->GetVisible());
  EXPECT_TRUE(view->GetLocalBounds().Contains(
      view->close_button_for_testing()->bounds()));
  EXPECT_TRUE(view->GetLocalBounds().Contains(
      view->open_button_for_testing()->bounds()));
  EXPECT_FALSE(view->CardBounds().Intersects(
      view->close_button_for_testing()->bounds()));
}

}  // namespace
}  // namespace arcium::test
