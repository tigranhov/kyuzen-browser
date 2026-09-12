// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// SidebarTabModel is the sidebar's only door onto profiles: profiles(),
// CreateProfileForSpace, SetSpaceProfile, RenameProfile, SetProfileColor,
// ClearProfileData and DeleteProfile, plus profile_id on each SidebarSpace.
// Every one of those forwards to ArciumModel or to the free functions in
// profile_actions.h/profile_data.h, which have their own tests -- but
// nothing before this file called the forwarding methods themselves, so a
// broken or deleted forward would still build and every existing test would
// stay green. These tests go through SidebarTabModel and check the real
// effect (a moved tab, a cleared cookie, a renamed profile), not just that a
// call returns.

#include <algorithm>
#include <memory>
#include <optional>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"

namespace arcium::test {
namespace {

using SidebarTabModelProfilesTest = ProfileBrowserTest;

// The real SidebarTabModel a window's sidebar draws from, or null if the
// window has none (it always does here: kArciumSidebar is on by default and
// every browser this fixture makes is TYPE_NORMAL).
SidebarTabModel* SidebarModelFor(Browser* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->arcium_sidebar()) {
    return nullptr;
  }
  return browser_view->arcium_sidebar()->model_for_testing();
}

std::optional<SidebarSpace> FindSpace(SidebarTabModel* sidebar, SpaceId id) {
  for (const SidebarSpace& space : sidebar->spaces()) {
    if (space.id == id) {
      return space;
    }
  }
  return std::nullopt;
}

// CreateProfileForSpace: makes a profile, and profiles()/spaces() (and the
// space's tab, reopened in the new partition) show it -- not only
// ArciumModel's own view of itself.
IN_PROC_BROWSER_TEST_F(SidebarTabModelProfilesTest,
                       CreateProfileForSpacePutsTheSpaceOnItThroughTheSidebar) {
  SidebarTabModel* sidebar = SidebarModelFor(browser());
  ASSERT_TRUE(sidebar);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "default");
  const int index = strip()->active_index();
  const SpaceId space = model()->default_space_id();
  ASSERT_EQ(1u, sidebar->profiles().size());

  sidebar->CreateProfileForSpace(space, u"Work", 3);

  ASSERT_EQ(2u, sidebar->profiles().size());
  EXPECT_EQ(u"Work", sidebar->profiles()[1].name);
  EXPECT_EQ(3, sidebar->profiles()[1].color);
  const ProfileId work = sidebar->profiles()[1].id;

  const std::optional<SidebarSpace> row = FindSpace(sidebar, space);
  ASSERT_TRUE(row.has_value());
  EXPECT_EQ(work, row->profile_id);

  content::WebContents* after = strip()->GetWebContentsAt(index);
  EXPECT_EQ(PartitionDomainForProfile(work), PartitionOf(after));
  ASSERT_TRUE(content::WaitForLoadStop(after));
  // Logged out: the new partition has never seen this site.
  EXPECT_EQ("", ReadCookie(after));
}

// SetSpaceProfile: moves the space and reopens its tab, the same effect
// ChangingASpacesProfileReopensItsTabsEverywhere
// (profile_lifecycle_browsertest.cc) proves for MoveSpaceToProfile itself, but
// reached through the sidebar's own forwarding method instead of the free
// function.
IN_PROC_BROWSER_TEST_F(SidebarTabModelProfilesTest,
                       SetSpaceProfileForwardsAndReopensTheTab) {
  SidebarTabModel* sidebar = SidebarModelFor(browser());
  ASSERT_TRUE(sidebar);
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  const int index = strip()->active_index();
  ASSERT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(active()));

  sidebar->SetSpaceProfile(work, DefaultProfileId());

  content::WebContents* after = strip()->GetWebContentsAt(index);
  EXPECT_EQ("", PartitionOf(after));
  const std::optional<SidebarSpace> row = FindSpace(sidebar, work);
  ASSERT_TRUE(row.has_value());
  EXPECT_EQ(DefaultProfileId(), row->profile_id);
  ASSERT_TRUE(content::WaitForLoadStop(after));
  EXPECT_EQ("", ReadCookie(after));
}

IN_PROC_BROWSER_TEST_F(SidebarTabModelProfilesTest,
                       RenameProfileForwardsToTheModel) {
  SidebarTabModel* sidebar = SidebarModelFor(browser());
  ASSERT_TRUE(sidebar);
  const ProfileId work = model()->AddProfile(u"Work", 1);

  sidebar->RenameProfile(work, u"Renamed");

  ASSERT_TRUE(model()->GetProfile(work));
  EXPECT_EQ(u"Renamed", model()->GetProfile(work)->name);
  const auto profiles = sidebar->profiles();
  const auto it =
      std::find_if(profiles.begin(), profiles.end(),
                   [&](const SidebarProfile& p) { return p.id == work; });
  ASSERT_NE(profiles.end(), it);
  EXPECT_EQ(u"Renamed", it->name);
}

IN_PROC_BROWSER_TEST_F(SidebarTabModelProfilesTest,
                       SetProfileColorForwardsToTheModel) {
  SidebarTabModel* sidebar = SidebarModelFor(browser());
  ASSERT_TRUE(sidebar);
  const ProfileId work = model()->AddProfile(u"Work", 1);

  sidebar->SetProfileColor(work, 5);

  ASSERT_TRUE(model()->GetProfile(work));
  EXPECT_EQ(5, model()->GetProfile(work)->color);
}

// ClearProfileData: the sidebar's own version of
// ClearingOneProfileLeavesTheOthersAlone (profile_lifecycle_browsertest.cc),
// reached through SidebarTabModel rather than ClearArciumProfileData.
IN_PROC_BROWSER_TEST_F(SidebarTabModelProfilesTest,
                       ClearProfileDataForwardsAndClearsOnlyThatProfile) {
  SidebarTabModel* sidebar = SidebarModelFor(browser());
  ASSERT_TRUE(sidebar);
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

  sidebar->ClearProfileData(work_profile);

  EXPECT_TRUE(
      base::test::RunUntil([&] { return ReadCookie(work_tab).empty(); }));
  EXPECT_EQ("who=default", ReadCookie(default_tab));
}

// DeleteProfile: the sidebar's own version of
// DeletingAProfileMovesItsSpacesAndLogsThemOut, reached through
// SidebarTabModel.
IN_PROC_BROWSER_TEST_F(SidebarTabModelProfilesTest,
                       DeleteProfileForwardsAndMovesItsSpacesToDefault) {
  SidebarTabModel* sidebar = SidebarModelFor(browser());
  ASSERT_TRUE(sidebar);
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  const int index = strip()->active_index();

  sidebar->DeleteProfile(work_profile);

  EXPECT_EQ(1u, sidebar->profiles().size());
  const std::optional<SidebarSpace> row = FindSpace(sidebar, work);
  ASSERT_TRUE(row.has_value());
  EXPECT_EQ(DefaultProfileId(), row->profile_id);
  content::WebContents* after = strip()->GetWebContentsAt(index);
  EXPECT_EQ("", PartitionOf(after));
  ASSERT_TRUE(content::WaitForLoadStop(after));
  EXPECT_EQ("", ReadCookie(after));
}

// Finding 1: BrowserSidebarController::MaybeCreate gates only on
// is_type_normal(), not IsOffTheRecord(), so an incognito window gets a full
// sidebar backed by its own store-less ArciumModel. CreateProfileForSpace
// adds a profile to that model and then tries to attach the space to it
// through SetSpaceProfile -> MoveSpaceToProfile, which refuses off the
// record. Without the undo in CreateProfileForSpace, the profile would stay
// in profiles() forever, attached to nothing and pointing at storage the
// user can never reach: a visible orphan. This is the regression test for
// that undo -- delete the RemoveProfile call it guards and this test fails.
IN_PROC_BROWSER_TEST_F(SidebarTabModelProfilesTest,
                       CreateProfileForSpaceUndoesItselfOffTheRecord) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito->GetProfile()->IsOffTheRecord());
  SidebarTabModel* sidebar = SidebarModelFor(incognito);
  ASSERT_TRUE(sidebar);
  ASSERT_EQ(1u, sidebar->profiles().size());
  const SpaceId space = sidebar->spaces()[0].id;

  sidebar->CreateProfileForSpace(space, u"Work", 2);

  // Not two: the add must have been undone once the attach refused, off the
  // record.
  EXPECT_EQ(1u, sidebar->profiles().size());
  const std::optional<SidebarSpace> row = FindSpace(sidebar, space);
  ASSERT_TRUE(row.has_value());
  EXPECT_EQ(DefaultProfileId(), row->profile_id);

  CloseBrowserSynchronously(incognito);
}

}  // namespace
}  // namespace arcium::test
