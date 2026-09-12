// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_data.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_space.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/clear_data_warning.h"
#include "arcium/ui/browser/profile_actions.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/performance_manager/public/mojom/lifecycle.mojom.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"

namespace arcium::test {
namespace {

using ProfileLifecycleTest = ProfileBrowserTest;

// Chromium throws a background tab away under memory pressure and builds a
// new contents for it; without the hook that contents is in the default
// partition, and the tab comes back logged out.
IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest,
                       ADiscardedTabComesBackInItsProfile) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  const int index = strip()->active_index();

  // A tab on screen is never discarded; leave it for another one.
  switcher()->SwitchTo(model()->default_space_id());

  resource_coordinator::TabLifecycleUnitExternal* unit =
      resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
          strip()->GetWebContentsAt(index));
  ASSERT_TRUE(unit);
  ASSERT_TRUE(unit->DiscardTab(mojom::LifecycleUnitDiscardReason::PROACTIVE));

  content::WebContents* replacement = strip()->GetWebContentsAt(index);
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(replacement));
  // The tag lives on the contents the discard just threw away. Without
  // carrying it over the tab reads as belonging to the model's first space:
  // it would be written to the session file under the wrong profile, and the
  // partition guard would send its next load into storage it never used.
  EXPECT_EQ(work, SpaceTagOf(replacement));

  // Back to the space the tab belongs to and on screen in it, the way a user
  // returns to a tab Chromium threw away. Whether a discarded tab is worth
  // reloading is Chromium's own decision, and here it keeps the entry it
  // already has rather than navigating again, so the page is asked for
  // directly instead of waiting on a reload that is never scheduled.
  switcher()->SwitchTo(work);
  strip()->ActivateTabAt(index);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  EXPECT_EQ("who=work", ReadCookie(strip()->GetWebContentsAt(index)));
}

// Profiles belong to the browser, not to a window, so a change reaches
// every window's tabs.
IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest,
                       ChangingASpacesProfileReopensItsTabsEverywhere) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  const int index = strip()->active_index();
  content::WebContents* before = active();

  Browser* second = CreateBrowser(browser()->GetProfile());
  SpaceSwitcher* second_switcher =
      SpaceSwitcher::FromTabStripModel(second->tab_strip_model());
  ASSERT_TRUE(second_switcher);
  second_switcher->SwitchTo(work);
  ui_test_utils::NavigateToURLWithDisposition(
      second, url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  const int second_index = second->tab_strip_model()->active_index();
  ASSERT_EQ(
      PartitionDomainForProfile(work_profile),
      PartitionOf(second->tab_strip_model()->GetWebContentsAt(second_index)));

  MoveSpaceToProfile(browser()->GetProfile(), work, DefaultProfileId());

  content::WebContents* after = strip()->GetWebContentsAt(index);
  EXPECT_NE(before, after);
  EXPECT_EQ("", PartitionOf(after));
  EXPECT_EQ(work, switcher()->SpaceOfTabAt(index));
  ASSERT_TRUE(content::WaitForLoadStop(after));
  EXPECT_EQ(url, after->GetLastCommittedURL());
  // Logged out, because Default has never seen this site.
  EXPECT_EQ("", ReadCookie(after));
  EXPECT_EQ("", PartitionOf(
                    second->tab_strip_model()->GetWebContentsAt(second_index)));
}

IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest,
                       ClearingOneProfileLeavesTheOthersAlone) {
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* default_tab = active();
  SetCookie(default_tab, "default");

  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  SetCookie(work_tab, "work");

  ProfileId home_profile;
  AddSpaceOnNewProfile(u"Home", &home_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* home_tab = active();
  SetCookie(home_tab, "home");

  base::RunLoop loop;
  ClearArciumProfileData(browser()->GetProfile(), work_profile,
                         loop.QuitClosure());
  loop.Run();

  EXPECT_EQ("", ReadCookie(work_tab));
  EXPECT_EQ("who=home", ReadCookie(home_tab));
  EXPECT_EQ("who=default", ReadCookie(default_tab));
  // Decision 8: clearing does not reload anything, as Chrome's own clear
  // does not.
  EXPECT_EQ(url, work_tab->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest,
                       DeletingAProfileMovesItsSpacesAndLogsThemOut) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  const int index = strip()->active_index();

  DeleteArciumProfile(browser()->GetProfile(), work_profile);

  EXPECT_EQ(1u, model()->profiles().size());
  EXPECT_EQ(DefaultProfileId(), model()->ProfileOfSpace(work));
  content::WebContents* after = strip()->GetWebContentsAt(index);
  EXPECT_EQ("", PartitionOf(after));
  ASSERT_TRUE(content::WaitForLoadStop(after));
  EXPECT_EQ("", ReadCookie(after));
}

IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest, DefaultCannotBeDeleted) {
  DeleteArciumProfile(browser()->GetProfile(), DefaultProfileId());
  EXPECT_EQ(1u, model()->profiles().size());
  EXPECT_EQ(DefaultProfileId(), model()->profiles()[0].id);
}

// Regression attempt for the crash the previous task fixed: a hidden tab
// whose space no longer names the profile its storage was built on, and
// whose reload is asked for from inside the very TabStripModel operation
// that shows it again. Before the fix, PartitionGuardThrottle relocated the
// page on the spot, from inside ActivateTabAt's own reentrancy guard, and
// TabStripModel::ReentrancyCheck CHECK-crashed the browser
// (tab_strip_model.cc:141). The fix posts the relocation instead
// (ReopenInRightStorage in partition_guard_throttle.cc). Reproducing the
// crash needs a real mismatch under a real in-strip reload: a manual
// SetNeedsReload() stands in for whichever seam left one pending, because
// two earlier attempts through Chromium's own discard found NeedsReload()
// already false (a discarded tab is not worth reloading on its own, so nothing
// ever starts a load to catch).
IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest,
                       AStorageMismatchThatReloadsFromInsideActivateSurvives) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  const int index = strip()->active_index();
  content::WebContents* contents = strip()->GetWebContentsAt(index);
  ASSERT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(contents));

  // Off screen, the way a background tab of the space is when its profile
  // changes elsewhere.
  switcher()->SwitchTo(model()->default_space_id());

  // The model now disagrees with the storage the tab already holds -- as if
  // its space had changed profile without the tab having been reopened for
  // it yet.
  model()->SetSpaceProfile(work, DefaultProfileId());

  // Stands in for whatever would otherwise have left a reload pending; the
  // point under test is what happens when it fires from inside ActivateTabAt,
  // not how it came to be pending.
  contents->GetController().SetNeedsReload();

  // Shows the tab again, from inside a TabStripModel operation. If
  // NavigationControllerImpl::SetActive's own reload starts synchronously
  // here, the throttle cancels it and posts the relocation from the same
  // call stack; draining the queue after is what lets that posted task run
  // before this test goes looking for its result.
  switcher()->SwitchTo(work);
  strip()->ActivateTabAt(index);
  base::RunLoop().RunUntilIdle();

  // The browser is still standing. The cancelled reload leaves the original
  // tab showing what it already had, unchanged; the relocation opens the
  // same address in a tab of its own, the way any other new tab is made, so
  // that one is what should be sitting in the storage the space now names.
  content::WebContents* relocated = nullptr;
  for (int i = 0; i < strip()->count(); ++i) {
    content::WebContents* candidate = strip()->GetWebContentsAt(i);
    if (candidate != contents && candidate->GetVisibleURL() == url) {
      relocated = candidate;
      break;
    }
  }
  ASSERT_TRUE(relocated) << "the guard did not relocate the mismatched page";
  ASSERT_TRUE(content::WaitForLoadStop(relocated));
  EXPECT_EQ("", PartitionOf(relocated));
}

// The point of the warning's wider answer: it really does reach a space's
// own logins, which Chrome's own removal never touches.
IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest,
                       ClearingEveryProfileReachesTheSpacesOwnLogins) {
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* default_tab = active();
  SetCookie(default_tab, "default");

  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  SetCookie(work_tab, "work");

  SetClearDataWarningAnswerForTesting(true);
  bool removal_resumed = false;
  ASSERT_TRUE(AskWhichProfilesToClear(
      default_tab,
      base::BindLambdaForTesting([&] { removal_resumed = true; })));
  EXPECT_TRUE(removal_resumed);

  EXPECT_TRUE(
      base::test::RunUntil([&] { return ReadCookie(work_tab).empty(); }));
  // Default is Chrome's own removal's job, and that is what resumes here
  // rather than running inside this test.
  EXPECT_EQ("who=default", ReadCookie(default_tab));
  SetClearDataWarningAnswerForTesting(std::nullopt);
}

}  // namespace
}  // namespace arcium::test
