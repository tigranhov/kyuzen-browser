// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
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

// content creates a popup with no opener in the default partition and
// cannot call into Arcium; the guard is what puts it right.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest,
                       APopupWithNoOpenerIsReopenedInTheProfile) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  SetCookie(work_tab, "work");
  const int tabs_before = strip()->count();

  // target=_blank, which is noopener by default.
  ASSERT_TRUE(
      content::ExecJs(work_tab, "document.getElementById('blank').click()"));

  const GURL opened_url = PageUrl("a.test", "blank");
  content::WebContents* opened = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    opened = FindTab(opened_url);
    return opened && opened->GetLastCommittedURL() == opened_url;
  }));
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(opened));
  EXPECT_EQ("who=work", ReadCookie(opened));
  // The empty tab the popup arrived in is gone again: one tab added, not two.
  EXPECT_EQ(tabs_before + 1, strip()->count());
}

// Decision 7 from the other side: a browser page typed into a profile's tab
// opens in shared storage, in a tab of its own, and the page behind it stays.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest,
                       ABrowserPageInAProfileTabMovesToSharedStorage) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL page = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), page, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();

  const GURL version("chrome://version/");
  work_tab->GetController().LoadURL(version, content::Referrer(),
                                    ui::PAGE_TRANSITION_TYPED, std::string());
  content::WebContents* opened = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    opened = FindTab(version);
    return opened && opened->GetLastCommittedURL() == version;
  }));
  EXPECT_EQ("", PartitionOf(opened));
  // The tab the user was on still shows its page.
  EXPECT_EQ(page, work_tab->GetLastCommittedURL());
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(work_tab));
}

// Traced twice as doing nothing: InsertBlankTab's about:blank tab never
// keeps the fixed-partition SiteInstance it is given, and the browsing
// instance never latches it either, so the first real page loaded out of an
// empty space's blank tab does not inherit that partition. The guard is the
// only thing that isolates an empty space -- this proves it end to end.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest,
                       AnEmptySpacesBlankTabStillIsolatesItsFirstRealLoad) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  // AddSpaceOnNewProfile switches to the new space, which has no tabs, so
  // the switch lands on a freshly inserted blank tab.
  content::WebContents* blank_tab = active();
  // Never navigated -- content::WebContents::Create with no LoadURL call --
  // so there is nothing to check but that it is still the initial entry;
  // GetLastCommittedURL() is empty rather than about:blank until it commits
  // a real navigation.
  content::NavigationEntry* initial_entry =
      blank_tab->GetController().GetLastCommittedEntry();
  ASSERT_TRUE(initial_entry && initial_entry->IsInitialEntry());

  // A same-tab load, like a quick entry sending the blank tab somewhere:
  // NavigateToURLWithDisposition's CURRENT_TAB helper asserts the navigation
  // stays in the same WebContents, which does not hold here -- the guard
  // cancels it and reopens it in a new tab, exactly the relocation this test
  // exists to prove.
  const GURL url = PageUrl("a.test", "one");
  blank_tab->GetController().LoadURL(url, content::Referrer(),
                                     ui::PAGE_TRANSITION_TYPED, std::string());
  content::WebContents* loaded = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    loaded = FindTab(url);
    return loaded && loaded->GetLastCommittedURL() == url;
  }));
  ASSERT_TRUE(content::WaitForLoadStop(loaded));
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(loaded));

  SetCookie(loaded, "work");
  EXPECT_EQ("who=work", ReadCookie(loaded));

  switcher()->SwitchTo(model()->default_space_id());
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* default_tab = active();
  EXPECT_EQ("", PartitionOf(default_tab));
  EXPECT_EQ("", ReadCookie(default_tab));
}

}  // namespace
}  // namespace arcium::test
