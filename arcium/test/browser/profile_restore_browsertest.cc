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
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
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
                       PRE_ARestoredTabBuildsStorageAtCreationNotAtLoad) {
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

// This used to assert the opposite and fail on purpose (see the ledger,
// task 4 and its review): the hoped-for behaviour was that a profile whose
// tabs are all unloaded costs nothing at startup, but that is false for a
// profile with a *restored* tab. `CreateRestoredTab` builds that tab's
// WebContents with a SiteInstance already fixed to the profile's partition
// (patches/0182-restored-tab-profile-storage.patch), and a fixed-partition
// SiteInstance cannot join its BrowsingInstance's default site instance
// group (content/browser/site_instance_impl.cc). That forces
// WebContentsImpl's constructor to look up a process regardless of
// `kNoRendererProcess`, and that lookup reaches GetStoragePartition with
// creation permitted (content/public/browser/browser_context.h) -- so the
// partition, its storage contexts and its network context are built the
// moment the tab is created, not when it is first loaded. Only a profile
// with no restored tab at all still costs nothing (§12 of the stage 3b
// design, corrected alongside this test).
//
// What survives is the other half of the promise -- no renderer *process*
// runs until the tab is clicked -- and that is proven separately below in
// ARestoredTabsProcessIsNotSpawnedUntilClicked, because a process host is
// an object and an OS process is a different thing.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest,
                       ARestoredTabBuildsStorageAtCreationNotAtLoad) {
  ASSERT_EQ(2u, model()->spaces().size());
  const SpaceId work = model()->spaces()[1].id;
  const ProfileId work_profile = model()->ProfileOfSpace(work);
  // EXPECT, not ASSERT: this is the surprising true behaviour, proven by
  // measurement rather than assumed, and the click below is worth checking
  // even if this line ever regresses back to the hoped-for laziness -- the
  // same shape this test carried when it was still the deliberate failure.
  EXPECT_TRUE(IsPartitionLoaded(browser()->GetProfile(), work_profile));

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
  EXPECT_EQ("who=work", ReadCookie(work_tab));
}

IN_PROC_BROWSER_TEST_F(ProfileRestoreTest,
                       PRE_ARestoredTabsProcessIsNotSpawnedUntilClicked) {
  RestoreSessionAtNextLaunch();
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  switcher()->SwitchTo(model()->default_space_id());
  FlushSessionAndModel();
}

// The open question the ledger carried into this task: does a restored tab
// in its own profile spawn a real renderer process at launch, breaking
// R3.9's no-extra-processes promise? Settled here by counting, not by
// reading: `RenderProcessHost::GetCurrentRenderProcessCountForTesting()` is
// Content's own definition of how many renderer processes actually exist
// (it counts a host only when `IsInitializedAndNotDead()`, i.e. Init() was
// called and the OS process has not died -- the spare renderer excluded).
// A process *host* is created for the restored tab, per the test above, but
// a host is only an object until something asks it to do work.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest,
                       ARestoredTabsProcessIsNotSpawnedUntilClicked) {
  ASSERT_EQ(2u, model()->spaces().size());
  const SpaceId work = model()->spaces()[1].id;
  content::WebContents* work_tab = nullptr;
  for (int i = 0; i < strip()->count(); ++i) {
    if (switcher()->SpaceOfTabAt(i) == work) {
      work_tab = strip()->GetWebContentsAt(i);
    }
  }
  ASSERT_TRUE(work_tab);

  content::RenderProcessHost* host =
      work_tab->GetPrimaryMainFrame()->GetProcess();
  ASSERT_TRUE(host);
  EXPECT_FALSE(host->IsInitializedAndNotDead());
  const int before_click =
      content::RenderProcessHost::GetCurrentRenderProcessCountForTesting();

  content::TestNavigationObserver observer(work_tab);
  strip()->ActivateTabAt(strip()->GetIndexOfWebContents(work_tab));
  observer.Wait();

  EXPECT_TRUE(host->IsInitializedAndNotDead());
  EXPECT_EQ(
      before_click + 1,
      content::RenderProcessHost::GetCurrentRenderProcessCountForTesting());
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
