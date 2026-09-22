// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Two pages sharing the screen, against a real browser: what happens when one
// of them is moved out of its space, and what comes back after a relaunch.
// Both need real storage and a real session file, which is why they are here
// rather than in a unit test.

#include "arcium/browser/model/arcium_model.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/browser/split_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium::test {
namespace {

using SplitViewTest = ProfileBrowserTest;

SplitController* Split(Browser* browser) {
  return BrowserView::GetBrowserViewForBrowser(browser)
      ->arcium_sidebar()
      ->split();
}

// Opens `url` in a new foreground tab of the active space.
void OpenTab(Browser* browser, const GURL& url) {
  ui_test_utils::NavigateToURLWithDisposition(
      browser, url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
}

IN_PROC_BROWSER_TEST_F(SplitViewTest, MovingOneHalfToAnotherSpaceEndsTheSplit) {
  const SpaceId home = switcher()->active_space();
  const SpaceId work = model()->AddSpace(u"Work");
  ASSERT_NE(home, work);

  OpenTab(browser(), PageUrl("a.test", ""));
  tabs::TabInterface* const first = strip()->GetActiveTab();
  OpenTab(browser(), PageUrl("b.test", ""));
  tabs::TabInterface* const second = strip()->GetActiveTab();

  // Handles rather than indices from here on: forming a split reorders the
  // strip to put its two tabs together.
  ASSERT_TRUE(Split(browser())->SplitWithActive(strip()->GetIndexOfTab(first)));
  ASSERT_EQ(2u, strip()->GetForegroundTabs().size());
  const int tabs_before = strip()->count();

  switcher()->MoveTabToSpace(strip()->GetIndexOfTab(first), work);

  EXPECT_EQ(tabs_before, strip()->count())
      << "the move left a page behind in a second tab";
  for (int i = 0; i < strip()->count(); ++i) {
    EXPECT_FALSE(strip()->GetSplitForTab(i).has_value())
        << "tab " << i << " is still sharing the screen after the move";
  }
  // The control: everything above says a split went away, which is also what
  // a switcher wired to nothing would report. The tab that moved must now be
  // in the other space, which is what the move was for.
  EXPECT_EQ(work, switcher()->SpaceOfTabAt(strip()->GetIndexOfTab(first)));
  EXPECT_EQ(home, switcher()->SpaceOfTabAt(strip()->GetIndexOfTab(second)));
}

}  // namespace
}  // namespace arcium::test
