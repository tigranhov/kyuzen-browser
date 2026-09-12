// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_data.h"

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_partition.h"
#include "base/files/file_util.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class ProfileDataTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

// The sweep deletes every directory the keep list does not name, so an
// answer given before the model has been read would erase every profile.
TEST_F(ProfileDataTest, NothingIsKeptUntilTheModelHasBeenRead) {
  ArciumProfileState::GetForBrowserContext(&profile_);
  EXPECT_FALSE(PartitionPathsToKeep(&profile_).has_value());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(PartitionPathsToKeep(&profile_).has_value());
}

TEST_F(ProfileDataTest, NothingIsKeptWhenTheModelCouldNotBeRead) {
  ASSERT_TRUE(base::WriteFile(ArciumProfileState::ModelPath(profile_.GetPath()),
                              "{ not json"));
  ArciumProfileState::GetForBrowserContext(&profile_);
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(PartitionPathsToKeep(&profile_).has_value());
}

TEST_F(ProfileDataTest, EveryProfilesDirectoryIsKept) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(&profile_);
  task_environment_.RunUntilIdle();
  const ProfileId work = state->model()->AddProfile(u"Work", 1);
  const ProfileId home = state->model()->AddProfile(u"Home", 2);

  const std::optional<std::unordered_set<base::FilePath>> paths =
      PartitionPathsToKeep(&profile_);
  ASSERT_TRUE(paths);
  EXPECT_EQ(2u, paths->size());
  EXPECT_TRUE(paths->contains(PartitionDirectory(profile_.GetPath(), work)));
  EXPECT_TRUE(paths->contains(PartitionDirectory(profile_.GetPath(), home)));
  // Default is Chromium's own partition and is never swept.
  EXPECT_FALSE(paths->contains(base::FilePath()));
}

// Nothing may be built to answer this: the cleanup runs at startup, and
// building every profile's storage there is what R3.9 exists to avoid.
TEST_F(ProfileDataTest, AskingWhatToKeepBuildsNothing) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(&profile_);
  task_environment_.RunUntilIdle();
  const ProfileId work = state->model()->AddProfile(u"Work", 1);
  EXPECT_TRUE(PartitionPathsToKeep(&profile_).has_value());
  EXPECT_FALSE(IsPartitionLoaded(&profile_, work));
}

}  // namespace
}  // namespace arcium
