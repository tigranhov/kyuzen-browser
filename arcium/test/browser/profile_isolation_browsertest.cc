// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/point.h"

namespace arcium::test {
namespace {

using ProfileIsolationTest = ProfileBrowserTest;

IN_PROC_BROWSER_TEST_F(ProfileIsolationTest, TheWindowHasASidebarAndAModel) {
  ASSERT_TRUE(switcher());
  EXPECT_TRUE(state()->model_load_finished());
  EXPECT_EQ("", PartitionOf(active()));
}

// The whole point of the stage: the same site, two spaces, two logins.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest,
                       ASpaceOnItsOwnProfileLogsInSeparately) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(work_tab));
  EXPECT_EQ(work,
            switcher()->SpaceOfTabAt(strip()->GetIndexOfWebContents(work_tab)));
  SetCookie(work_tab, "work");

  switcher()->SwitchTo(model()->default_space_id());
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* default_tab = active();
  EXPECT_EQ("", PartitionOf(default_tab));
  EXPECT_EQ("", ReadCookie(default_tab));
  SetCookie(default_tab, "default");

  EXPECT_EQ("who=work", ReadCookie(work_tab));
  EXPECT_EQ("who=default", ReadCookie(default_tab));
}

// A Cmd+click: a real click with a real gesture, through the same seam.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest,
                       ALinkOpenedInANewTabKeepsTheProfile) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  SetCookie(work_tab, "work");

  ui_test_utils::TabAddedWaiter waiter(browser());
  const int x = content::EvalJs(work_tab,
                                "Math.round(document.getElementById('same')."
                                "getBoundingClientRect().left + 4)")
                    .ExtractInt();
  const int y = content::EvalJs(work_tab,
                                "Math.round(document.getElementById('same')."
                                "getBoundingClientRect().top + 4)")
                    .ExtractInt();
  content::SimulateMouseClickAt(work_tab, blink::WebInputEvent::kMetaKey,
                                blink::WebMouseEvent::Button::kLeft,
                                gfx::Point(x, y));
  waiter.Wait();

  content::WebContents* opened = FindTab(PageUrl("a.test", "same"));
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(opened));
  EXPECT_EQ("who=work", ReadCookie(opened));
}

// What an extension's tabs.create, or a menu command, does: a new tab from
// a tab of a space that is not the one on screen, and with no gesture, so
// nothing but the hook's own rule can place it.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest,
                       ANewTabFollowsItsSourceNotTheScreen) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  switcher()->SwitchTo(model()->default_space_id());

  NavigateParams params(browser(), PageUrl("a.test", "two"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  params.source_contents = work_tab;
  params.user_gesture = false;
  Navigate(&params);

  content::WebContents* opened = FindTab(PageUrl("a.test", "two"));
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(opened));
  EXPECT_EQ(work,
            switcher()->SpaceOfTabAt(strip()->GetIndexOfWebContents(opened)));
}

// Decision 7: an extension's pages must read the same storage wherever
// they are opened, or its options page would forget its settings.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest, ABrowserPageStaysInSharedStorage) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://version/"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  EXPECT_EQ("", PartitionOf(active()));
}

}  // namespace
}  // namespace arcium::test
