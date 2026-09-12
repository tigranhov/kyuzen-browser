// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/model_migration.h"

#include <optional>
#include <utility>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/model_serializer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

base::DictValue DictAtVersion(int version) {
  base::DictValue dict;
  dict.Set("version", version);
  dict.Set("spaces", base::ListValue());
  dict.Set("folders", base::ListValue());
  dict.Set("entries", base::ListValue());
  return dict;
}

TEST(ModelMigrationTest, AFileWithNoVersionIsRefused) {
  base::DictValue dict;
  dict.Set("entries", base::ListValue());
  EXPECT_FALSE(MigrateModelDict(std::move(dict)).has_value());
}

TEST(ModelMigrationTest, AFileFromTheFutureIsRefused) {
  EXPECT_FALSE(
      MigrateModelDict(DictAtVersion(kModelSchemaVersion + 1)).has_value());
}

TEST(ModelMigrationTest, AVersionBelowOneIsRefused) {
  EXPECT_FALSE(MigrateModelDict(DictAtVersion(0)).has_value());
}

TEST(ModelMigrationTest, AVersionOneFileComesBackAtTheCurrentVersion) {
  std::optional<base::DictValue> migrated = MigrateModelDict(DictAtVersion(1));
  ASSERT_TRUE(migrated.has_value());
  EXPECT_EQ(kModelSchemaVersion, migrated->FindInt("version"));
  // Nothing else was invented or dropped on the way through.
  EXPECT_TRUE(migrated->FindList("entries"));
  EXPECT_TRUE(migrated->FindList("folders"));
}

TEST(ModelMigrationTest, AFileAlreadyAtTheCurrentVersionIsPassedThrough) {
  base::DictValue dict = DictAtVersion(kModelSchemaVersion);
  dict.Set("marker", "kept");
  std::optional<base::DictValue> migrated = MigrateModelDict(std::move(dict));
  ASSERT_TRUE(migrated.has_value());
  EXPECT_EQ(kModelSchemaVersion, migrated->FindInt("version"));
  ASSERT_TRUE(migrated->FindString("marker"));
  EXPECT_EQ("kept", *migrated->FindString("marker"));
}

TEST(ModelMigrationTest, AVersionTwoSpaceGainsTheStageThreeDefaults) {
  base::DictValue dict = DictAtVersion(2);
  base::DictValue space;
  space.Set("id", SpaceId::Generate().value());
  space.Set("name", "Space");
  base::ListValue spaces;
  spaces.Append(std::move(space));
  dict.Set("spaces", std::move(spaces));

  std::optional<base::DictValue> migrated = MigrateModelDict(std::move(dict));
  ASSERT_TRUE(migrated.has_value());
  EXPECT_EQ(kModelSchemaVersion, migrated->FindInt("version"));
  const base::ListValue* list = migrated->FindList("spaces");
  ASSERT_TRUE(list);
  const base::DictValue* first = (*list)[0].GetIfDict();
  ASSERT_TRUE(first);
  EXPECT_EQ(0, first->FindInt("gradient"));
  ASSERT_TRUE(first->FindString("icon"));
  EXPECT_EQ("", *first->FindString("icon"));
}

TEST(ModelMigrationTest, AVersionThreeFileGainsTheDefaultProfileOnEverySpace) {
  base::DictValue dict = DictAtVersion(3);
  base::DictValue space;
  space.Set("id", "44444444-4444-4444-8444-444444444444");
  space.Set("name", "Space");
  dict.FindList("spaces")->Append(std::move(space));

  std::optional<base::DictValue> migrated = MigrateModelDict(std::move(dict));

  ASSERT_TRUE(migrated.has_value());
  EXPECT_EQ(kModelSchemaVersion, migrated->FindInt("version"));
  const base::ListValue* profiles = migrated->FindList("profiles");
  ASSERT_TRUE(profiles);
  ASSERT_EQ(1u, profiles->size());
  EXPECT_EQ(kDefaultProfileIdValue, *(*profiles)[0].GetDict().FindString("id"));
  EXPECT_EQ("Default", *(*profiles)[0].GetDict().FindString("name"));
  EXPECT_EQ(
      kDefaultProfileIdValue,
      *(*migrated->FindList("spaces"))[0].GetDict().FindString("profile_id"));
}

}  // namespace
}  // namespace arcium
