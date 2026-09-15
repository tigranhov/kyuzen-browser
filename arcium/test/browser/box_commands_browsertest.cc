// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The two things Stage 4b added to the command box: an address with a routing
// rule opens in the rule's space (R4.5), and the words of a command's name
// offer the command and run it (R4.1).

#include <optional>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/test/browser/sidebar_ui_browsertest_base.h"
#include "arcium/ui/browser/box_commands.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/command_box.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace arcium::test {
namespace {

class BoxCommandsTest : public SidebarUiTest {
 protected:
  ArciumProfileState* State() {
    return ArciumProfileState::GetForBrowserContext(browser()->GetProfile());
  }

  SpaceId ActiveSpace() {
    return SpaceSwitcher::FromTabStripModel(browser()->tab_strip_model())
        ->active_space();
  }

  // The rows the box is offering, once it has answered at least once.
  std::optional<int> FirstCommandId() {
    for (size_t i = 0; i < Box()->row_count_for_testing(); ++i) {
      if (Box()->row_for_testing(i).command_id) {
        return Box()->row_for_testing(i).command_id;
      }
    }
    return std::nullopt;
  }
};

IN_PROC_BROWSER_TEST_F(BoxCommandsTest, AnAddressWithARuleOpensInThatSpace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("work.test", "/title1.html");
  const SpaceId work = State()->model()->AddSpace(u"Work");
  State()->model()->SetRoutingRule("work.test", work);
  ASSERT_NE(work, ActiveSpace());

  OpenBox();
  Type(base::UTF8ToUTF16(url.spec()));
  WaitForRows();
  PressEnter();
  RunLoopUntilIdle();

  EXPECT_EQ(work, ActiveSpace()) << "the window stayed in the other space";
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(url, contents->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(BoxCommandsTest, AnAddressWithNoRuleStaysWhereYouAre) {
  // The control for the test above: without a rule nothing moves, so what
  // that one saw was the rule and not the box switching spaces on its own.
  ASSERT_TRUE(embedded_test_server()->Start());
  const SpaceId here = ActiveSpace();
  ASSERT_TRUE(State()->model()->AddSpace(u"Work").is_valid());

  OpenBox();
  Type(base::UTF8ToUTF16(
      embedded_test_server()->GetURL("play.test", "/title1.html").spec()));
  WaitForRows();
  PressEnter();
  RunLoopUntilIdle();

  EXPECT_EQ(here, ActiveSpace());
}

IN_PROC_BROWSER_TEST_F(BoxCommandsTest, ChoosingCloseTabClosesTheTabOnScreen) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  const int tabs_before = browser()->tab_strip_model()->count();

  OpenBox();
  Type(u"close ta");
  WaitForRows();
  ASSERT_EQ(std::optional<int>(IDC_CLOSE_TAB), FirstCommandId());
  PressEnter();
  RunLoopUntilIdle();

  EXPECT_EQ(tabs_before - 1, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(BoxCommandsTest, ChoosingNewSpaceMakesOne) {
  const size_t spaces_before = State()->model()->spaces().size();

  OpenBox();
  Type(u"new sp");
  WaitForRows();
  ASSERT_EQ(std::optional<int>(kBoxCommandNewSpace), FirstCommandId());
  PressEnter();
  RunLoopUntilIdle();

  EXPECT_EQ(spaces_before + 1, State()->model()->spaces().size());
}

}  // namespace
}  // namespace arcium::test
