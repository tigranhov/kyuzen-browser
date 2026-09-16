// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Launching a browser that is already running raises the window it already
// has instead of opening a second one. The tests call
// arcium::OpenSecondLaunchInExistingWindow directly, which is what the hook in
// patch 0230 does with the command line the process singleton hands over.

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/tab_space.h"
#include "arcium/test/browser/sidebar_ui_browsertest_base.h"
#include "arcium/ui/browser/second_launch.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium::test {
namespace {

class SecondLaunchTest : public SidebarUiTest {
 protected:
  ArciumProfileState* State() {
    return ArciumProfileState::GetForBrowserContext(browser()->GetProfile());
  }

  SpaceId ActiveSpace() {
    return SpaceSwitcher::FromTabStripModel(browser()->tab_strip_model())
        ->active_space();
  }

  bool Launch(const base::CommandLine& command_line) {
    const bool taken =
        OpenSecondLaunchInExistingWindow(command_line, base::FilePath());
    RunLoopUntilIdle();
    return taken;
  }

  base::CommandLine PlainLaunch() {
    return base::CommandLine(base::CommandLine::NO_PROGRAM);
  }
};

// Through Chromium's own entry point rather than through the function the
// hook calls, because what is being claimed is that the launch stops there:
// a test that only called the function would pass just as well if the hook
// were never applied, and a second window is exactly what it used to open.
IN_PROC_BROWSER_TEST_F(SecondLaunchTest, OpensNoSecondWindow) {
  const size_t windows_before = GetAllBrowserWindowInterfaces().size();
  const int tabs_before = browser()->tab_strip_model()->count();

  StartupBrowserCreator::ProcessCommandLineAlreadyRunning(
      PlainLaunch(), base::FilePath(),
      {browser()->GetProfile()->GetPath(), StartupProfileMode::kBrowserWindow});
  RunLoopUntilIdle();

  EXPECT_EQ(windows_before, GetAllBrowserWindowInterfaces().size());
  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count())
      << "a launch with nothing to open should open nothing";
}

IN_PROC_BROWSER_TEST_F(SecondLaunchTest, AnAddressOpensAsATabInThatWindow) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  const size_t windows_before = GetAllBrowserWindowInterfaces().size();
  const int tabs_before = browser()->tab_strip_model()->count();

  base::CommandLine command_line = PlainLaunch();
  command_line.AppendArg(url.spec());
  EXPECT_TRUE(Launch(command_line));

  EXPECT_EQ(windows_before, GetAllBrowserWindowInterfaces().size());
  ASSERT_EQ(tabs_before + 1, browser()->tab_strip_model()->count());
  EXPECT_EQ(
      url,
      browser()->tab_strip_model()->GetActiveWebContents()->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(SecondLaunchTest, AnAddressGoesToTheSpaceItsRuleNames) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("work.test", "/title1.html");
  const SpaceId work = State()->model()->AddSpace(u"Work");
  State()->model()->SetRoutingRule("work.test", work);
  ASSERT_NE(work, ActiveSpace());

  base::CommandLine command_line = PlainLaunch();
  command_line.AppendArg(url.spec());
  EXPECT_TRUE(Launch(command_line));

  content::WebContents* opened =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(opened);
  EXPECT_EQ(url, opened->GetVisibleURL());
  EXPECT_EQ(work, SpaceTagOf(opened)) << "the rule's space, not the one shown";
  EXPECT_EQ(work, ActiveSpace()) << "and the window goes there";
}

// A launch that means something other than "show me the browser" is left to
// Chromium, which is the only thing that knows how to build those windows.
IN_PROC_BROWSER_TEST_F(SecondLaunchTest,
                       DeclinesALaunchAskingForAnotherWindow) {
  base::CommandLine command_line = PlainLaunch();
  command_line.AppendSwitch(switches::kIncognito);

  EXPECT_FALSE(Launch(command_line));
}

}  // namespace
}  // namespace arcium::test
