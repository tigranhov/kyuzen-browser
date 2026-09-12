// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/profile_colors.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

TEST(ProfileColorsTest, EveryPresetHasANameAndAnOutOfRangeIndexDrawsTheFirst) {
  ASSERT_EQ(8u, ProfileColors().size());
  for (const ProfileColor& preset : ProfileColors()) {
    EXPECT_FALSE(preset.name.empty());
  }
  EXPECT_EQ(ProfileColors()[0].color, ProfileColorAt(-1).color);
  EXPECT_EQ(ProfileColors()[0].color, ProfileColorAt(99).color);
  EXPECT_EQ(ProfileColors()[3].color, ProfileColorAt(3).color);
}

TEST(FakeSidebarProfilesTest, StartsWithDefaultAndEverySpaceOnIt) {
  FakeSidebarModel model;
  ASSERT_EQ(1u, model.profiles().size());
  EXPECT_EQ(DefaultProfileId(), model.profiles()[0].id);
  EXPECT_EQ(DefaultProfileId(), model.spaces()[0].profile_id);
}

TEST(FakeSidebarProfilesTest, ANewSpaceStartsOnTheProfileOfTheSpaceYouAreIn) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 2);
  model.SetSpaceProfile(model.spaces()[0].id, work);

  model.AddSpace(u"New space");

  ASSERT_EQ(2u, model.spaces().size());
  EXPECT_EQ(work, model.spaces()[1].profile_id);
}

TEST(FakeSidebarProfilesTest, CreatingAProfileForASpacePutsTheSpaceOnIt) {
  FakeSidebarModel model;
  const SpaceId space = model.spaces()[0].id;
  model.CreateProfileForSpace(space, u"Work", 4);

  ASSERT_EQ(2u, model.profiles().size());
  EXPECT_EQ(u"Work", model.profiles()[1].name);
  EXPECT_EQ(4, model.profiles()[1].color);
  EXPECT_EQ(model.profiles()[1].id, model.spaces()[0].profile_id);
}

TEST(FakeSidebarProfilesTest, DeletingAProfileMovesItsSpacesToDefault) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.SetSpaceProfile(model.spaces()[0].id, work);

  model.DeleteProfile(work);

  ASSERT_EQ(1u, model.profiles().size());
  EXPECT_EQ(DefaultProfileId(), model.spaces()[0].profile_id);
}

TEST(FakeSidebarProfilesTest, DefaultCannotBeDeleted) {
  FakeSidebarModel model;
  model.DeleteProfile(DefaultProfileId());
  EXPECT_EQ(1u, model.profiles().size());
}

TEST(FakeSidebarProfilesTest, ClearingAProfileIsRecordedAndChangesNothingElse) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.ClearProfileData(work);
  ASSERT_EQ(1u, model.cleared_profiles_for_testing().size());
  EXPECT_EQ(work, model.cleared_profiles_for_testing()[0]);
  EXPECT_EQ(2u, model.profiles().size());
}

}  // namespace
}  // namespace arcium
