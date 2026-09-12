// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"

namespace arcium::test {
namespace {

using ProfileRestoreTest = ProfileBrowserTest;

// Two spaces, two profiles, one site, two logins -- then a restart.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest, PRE_ARestoredTabKeepsItsProfile) {
  RestoreSessionAtNextLaunch();
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");

  switcher()->SwitchTo(model()->default_space_id());
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "default");
  FlushSessionAndModel();
}

IN_PROC_BROWSER_TEST_F(ProfileRestoreTest, ARestoredTabKeepsItsProfile) {
  ASSERT_EQ(2u, model()->profiles().size());
  ASSERT_EQ(2u, model()->spaces().size());
  const SpaceId work = model()->spaces()[1].id;
  const ProfileId work_profile = model()->ProfileOfSpace(work);
  ASSERT_NE(DefaultProfileId(), work_profile);

  // Found by its space, not by its storage: asking a tab for its partition
  // would create it, which the next test is about.
  content::WebContents* work_tab = nullptr;
  for (int i = 0; i < strip()->count(); ++i) {
    if (switcher()->SpaceOfTabAt(i) == work) {
      work_tab = strip()->GetWebContentsAt(i);
    }
  }
  ASSERT_TRUE(work_tab);
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(work_tab));

  // R3.9 leaves a restored tab unloaded; clicking it is what loads it.
  content::TestNavigationObserver observer(work_tab);
  strip()->ActivateTabAt(strip()->GetIndexOfWebContents(work_tab));
  observer.Wait();
  EXPECT_EQ("who=work", ReadCookie(work_tab));
}

IN_PROC_BROWSER_TEST_F(ProfileRestoreTest,
                       PRE_ARestoredTabBuildsNoStorageUntilItLoads) {
  RestoreSessionAtNextLaunch();
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  // Leave the window on Default, so the restored Work tab is not the one
  // the restart loads.
  switcher()->SwitchTo(model()->default_space_id());
  FlushSessionAndModel();
}

// What the performance claim rests on: a profile whose tabs are all
// unloaded costs nothing at startup.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest,
                       ARestoredTabBuildsNoStorageUntilItLoads) {
  ASSERT_EQ(2u, model()->spaces().size());
  const SpaceId work = model()->spaces()[1].id;
  const ProfileId work_profile = model()->ProfileOfSpace(work);
  EXPECT_FALSE(IsPartitionLoaded(browser()->GetProfile(), work_profile));

  content::WebContents* work_tab = nullptr;
  for (int i = 0; i < strip()->count(); ++i) {
    if (switcher()->SpaceOfTabAt(i) == work) {
      work_tab = strip()->GetWebContentsAt(i);
    }
  }
  ASSERT_TRUE(work_tab);
  content::TestNavigationObserver observer(work_tab);
  strip()->ActivateTabAt(strip()->GetIndexOfWebContents(work_tab));
  observer.Wait();
  EXPECT_TRUE(IsPartitionLoaded(browser()->GetProfile(), work_profile));
}

// Cmd+Shift+T reaches the same tab-building function by another road.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest,
                       AReopenedClosedTabComesBackInItsProfile) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  strip()->CloseWebContentsAt(strip()->active_index(),
                              TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);

  chrome::RestoreTab(browser());
  content::WebContents* reopened = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    reopened = FindTab(url);
    return reopened != nullptr;
  }));
  ASSERT_TRUE(content::WaitForLoadStop(reopened));
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(reopened));
  EXPECT_EQ("who=work", ReadCookie(reopened));
}

// A login kept in a session cookie: Chromium restores those only for the
// default partition unless it is told otherwise, and A3.1 says both
// accounts are still signed in after a relaunch.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest, PRE_ASessionCookieSurvivesARestart) {
  RestoreSessionAtNextLaunch();
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work", /*session_only=*/true);
  FlushSessionAndModel();
}

IN_PROC_BROWSER_TEST_F(ProfileRestoreTest, ASessionCookieSurvivesARestart) {
  ASSERT_EQ(2u, model()->spaces().size());
  const SpaceId work = model()->spaces()[1].id;
  content::WebContents* work_tab = nullptr;
  for (int i = 0; i < strip()->count(); ++i) {
    if (switcher()->SpaceOfTabAt(i) == work) {
      work_tab = strip()->GetWebContentsAt(i);
    }
  }
  ASSERT_TRUE(work_tab);
  // Unlike PRE_ARestoredTabKeepsItsProfile's, this PRE_ test leaves the
  // window on the Work space, so the Work tab is the one on screen at quit --
  // the one R3.9 still loads itself at restart, same as Chromium's own
  // session restore has always done for whichever tab was showing. It may
  // already be done loading by the time this test body runs, so there is no
  // fresh navigation left for a click to start; wait for the load already in
  // flight (or already finished) rather than for one that will not happen.
  ASSERT_TRUE(content::WaitForLoadStop(work_tab));
  EXPECT_EQ("who=work", ReadCookie(work_tab));
}

}  // namespace
}  // namespace arcium::test
