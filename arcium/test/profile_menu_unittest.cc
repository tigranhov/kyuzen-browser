// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/profile_menu.h"

#include <string>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/test/test_app_activation.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/profile_colors.h"
#include "arcium/ui/sidebar/space_bar_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/test/views_test_base.h"

namespace arcium {
namespace {

class ProfileMenuTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    arcium::test::SuppressTestAppActivation();
    views::ViewsTestBase::SetUp();
  }
};

bool HasLabel(ui::MenuModel* model, const std::u16string& label) {
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetLabelAt(i) == label) {
      return true;
    }
  }
  return false;
}

TEST_F(ProfileMenuTest, ListsEveryProfileWithATickOnTheSpacesOwn) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.SetSpaceProfile(model.spaces()[0].id, work);
  ProfileMenu menu(&model, /*anchor=*/nullptr);
  menu.SetSpace(model.spaces()[0].id);

  EXPECT_TRUE(HasLabel(menu.model(), u"Default"));
  EXPECT_TRUE(HasLabel(menu.model(), u"Work"));
  EXPECT_FALSE(menu.IsCommandIdChecked(ProfileMenu::kProfileFirst + 0));
  EXPECT_TRUE(menu.IsCommandIdChecked(ProfileMenu::kProfileFirst + 1));
  EXPECT_TRUE(HasLabel(menu.model(), u"New profile…"));
  EXPECT_TRUE(HasLabel(menu.model(), u"Clear this profile's data…"));
  EXPECT_TRUE(HasLabel(menu.model(), u"Delete profile…"));
}

TEST_F(ProfileMenuTest, DefaultOffersNoDelete) {
  FakeSidebarModel model;
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);
  EXPECT_FALSE(HasLabel(menu.model(), u"Delete profile…"));
}

TEST_F(ProfileMenuTest, ChoosingAProfileMovesTheSpace) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);
  menu.ExecuteCommand(ProfileMenu::kProfileFirst + 1, 0);
  EXPECT_EQ(work, model.spaces()[0].profile_id);
}

TEST_F(ProfileMenuTest, NewProfileCreatesItOnThisSpace) {
  FakeSidebarModel model;
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);
  menu.ExecuteCommand(ProfileMenu::kNewProfile, 0);
  menu.SubmitNewProfileForTesting(u"Work", 3);
  ASSERT_EQ(2u, model.profiles().size());
  EXPECT_EQ(u"Work", model.profiles()[1].name);
  EXPECT_EQ(3, model.profiles()[1].color);
  EXPECT_EQ(model.profiles()[1].id, model.spaces()[0].profile_id);
}

TEST_F(ProfileMenuTest, AnEmptyNameMakesNoProfile) {
  FakeSidebarModel model;
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);
  menu.ExecuteCommand(ProfileMenu::kNewProfile, 0);
  menu.SubmitNewProfileForTesting(u"", 3);
  EXPECT_EQ(1u, model.profiles().size());
}

TEST_F(ProfileMenuTest, RenameAndRecolourActOnTheSpacesProfile) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.SetSpaceProfile(model.spaces()[0].id, work);
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);

  menu.ExecuteCommand(ProfileMenu::kRenameProfile, 0);
  menu.SubmitRenameForTesting(u"Job");
  menu.ExecuteCommand(ProfileMenu::kColorFirst + 5, 0);

  EXPECT_EQ(u"Job", model.profiles()[1].name);
  EXPECT_EQ(5, model.profiles()[1].color);
  EXPECT_TRUE(menu.IsCommandIdChecked(ProfileMenu::kColorFirst + 5));
}

TEST_F(ProfileMenuTest, ClearingAsksFirstAndSaysWhatStays) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.SetSpaceProfile(model.spaces()[0].id, work);
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);

  menu.ExecuteCommand(ProfileMenu::kClearData, 0);
  const std::u16string text = menu.pending_text_for_testing();
  EXPECT_NE(std::u16string::npos, text.find(u"Work"));
  EXPECT_NE(std::u16string::npos, text.find(u"signed out of every site"));
  EXPECT_NE(std::u16string::npos, text.find(u"History, passwords"));
  menu.AnswerForTesting(/*accept=*/false);
  EXPECT_TRUE(model.cleared_profiles_for_testing().empty());

  menu.ExecuteCommand(ProfileMenu::kClearData, 0);
  menu.AnswerForTesting(/*accept=*/true);
  ASSERT_EQ(1u, model.cleared_profiles_for_testing().size());
}

TEST_F(ProfileMenuTest, DeletingCountsSpacesAndTabsAndNamesDefault) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  const SpaceId first = model.spaces()[0].id;
  model.SetSpaceProfile(first, work);
  model.AddTabInSpaceForTesting(u"One", "https://w1.example/", first);
  model.AddTabInSpaceForTesting(u"Two", "https://w2.example/", first);
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(first);

  menu.ExecuteCommand(ProfileMenu::kDeleteProfile, 0);
  const std::u16string text = menu.pending_text_for_testing();
  EXPECT_NE(std::u16string::npos, text.find(u"Work"));
  EXPECT_NE(std::u16string::npos, text.find(u"erased for good"));
  EXPECT_NE(std::u16string::npos, text.find(u"1 space"));
  EXPECT_NE(std::u16string::npos, text.find(u"Default"));
  menu.AnswerForTesting(/*accept=*/true);
  EXPECT_EQ(1u, model.profiles().size());
  EXPECT_EQ(DefaultProfileId(), model.spaces()[0].profile_id);
}

TEST_F(ProfileMenuTest, TheBadgeShowsTheActiveSpacesProfile) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 2);
  SpaceBarView bar(&model);
  EXPECT_EQ(
      u"Profile: Default",
      bar.profile_badge_for_testing()->GetRenderedTooltipText(gfx::Point()));
  model.SetSpaceProfile(model.spaces()[0].id, work);
  EXPECT_EQ(
      u"Profile: Work",
      bar.profile_badge_for_testing()->GetRenderedTooltipText(gfx::Point()));
}

TEST_F(ProfileMenuTest, TheSpaceMenuCarriesTheProfileSubmenu) {
  FakeSidebarModel model;
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[0].id);
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kProfile));
  ASSERT_TRUE(bar.profile_menu_for_testing());
  EXPECT_TRUE(HasLabel(bar.profile_menu_for_testing()->model(), u"Default"));
}

}  // namespace
}  // namespace arcium
