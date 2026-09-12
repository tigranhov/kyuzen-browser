// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_partition.h"

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/entry_id.h"
#include "base/files/file_path.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

const ProfileId kWork =
    ProfileId::FromString("22222222-2222-4222-8222-222222222222");

class ProfilePartitionTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

TEST_F(ProfilePartitionTest, TheDefaultProfileHasNoPartitionOfItsOwn) {
  EXPECT_EQ("", PartitionDomainForProfile(DefaultProfileId()));
  EXPECT_FALSE(PartitionForProfile(&profile_, DefaultProfileId()));
  EXPECT_TRUE(
      PartitionDirectory(profile_.GetPath(), DefaultProfileId()).empty());
}

TEST_F(ProfilePartitionTest, AnotherProfileIsNamedFromItsId) {
  EXPECT_EQ("arcium-22222222-2222-4222-8222-222222222222",
            PartitionDomainForProfile(kWork));
  const std::optional<content::StoragePartitionConfig> config =
      PartitionForProfile(&profile_, kWork);
  ASSERT_TRUE(config);
  EXPECT_EQ(PartitionDomainForProfile(kWork), config->partition_domain());
  EXPECT_EQ("", config->partition_name());
  EXPECT_FALSE(config->in_memory());
}

TEST_F(ProfilePartitionTest, OnlyArciumDomainsAreArciums) {
  EXPECT_TRUE(IsArciumPartitionDomain(PartitionDomainForProfile(kWork)));
  EXPECT_FALSE(IsArciumPartitionDomain(""));
  EXPECT_FALSE(IsArciumPartitionDomain("arcium-"));
  // An extension's partition domain is its 32-letter id.
  EXPECT_FALSE(IsArciumPartitionDomain("abcdefghijklmnopabcdefghijklmnop"));
}

// Content hands the network context only this relative path, so it is the
// one fact the session-cookie hook has to recognise an Arcium profile by.
TEST_F(ProfilePartitionTest, APartitionPathIsRecognisedOnlyInItsExactShape) {
  const std::string domain = PartitionDomainForProfile(kWork);
  EXPECT_TRUE(IsArciumPartitionPath(base::FilePath(FILE_PATH_LITERAL("Storage"))
                                        .Append(FILE_PATH_LITERAL("ext"))
                                        .AppendASCII(domain)
                                        .Append(FILE_PATH_LITERAL("def"))));
  EXPECT_FALSE(
      IsArciumPartitionPath(base::FilePath(FILE_PATH_LITERAL("Storage"))
                                .Append(FILE_PATH_LITERAL("ext"))
                                .AppendASCII("abcdefghijklmnopabcdefghijklmnop")
                                .Append(FILE_PATH_LITERAL("def"))));
  EXPECT_FALSE(
      IsArciumPartitionPath(base::FilePath(FILE_PATH_LITERAL("Storage"))
                                .Append(FILE_PATH_LITERAL("ext"))
                                .AppendASCII(domain)));
  EXPECT_FALSE(IsArciumPartitionPath(base::FilePath()));
}

// Built by hand because content's own helper is internal, so this pins it
// to what content really uses.
TEST_F(ProfilePartitionTest, ThePartitionDirectoryIsWhereContentPutsIt) {
  content::StoragePartition* partition =
      profile_.GetStoragePartition(*PartitionForProfile(&profile_, kWork));
  EXPECT_EQ(PartitionDirectory(profile_.GetPath(), kWork)
                .Append(FILE_PATH_LITERAL("def")),
            partition->GetPath());
}

TEST_F(ProfilePartitionTest, AskingWhetherAPartitionIsLoadedNeverLoadsIt) {
  EXPECT_FALSE(IsPartitionLoaded(&profile_, kWork));
  EXPECT_FALSE(IsPartitionLoaded(&profile_, kWork));
  profile_.GetStoragePartition(*PartitionForProfile(&profile_, kWork));
  EXPECT_TRUE(IsPartitionLoaded(&profile_, kWork));
  EXPECT_FALSE(IsPartitionLoaded(&profile_, DefaultProfileId()));
}

TEST_F(ProfilePartitionTest, PagesAreSortedIntoTheirStorage) {
  EXPECT_EQ(PageStorage::kProfile, StorageForUrl(GURL("https://a.test/")));
  EXPECT_EQ(PageStorage::kProfile, StorageForUrl(GURL("data:text/html,hi")));
  EXPECT_EQ(PageStorage::kShared, StorageForUrl(GURL("chrome://settings/")));
  EXPECT_EQ(PageStorage::kShared,
            StorageForUrl(GURL("chrome-extension://abcdefghijklmnop/o.html")));
  EXPECT_EQ(PageStorage::kShared,
            StorageForUrl(GURL("devtools://devtools/bundled/inspector.html")));
  EXPECT_EQ(PageStorage::kShared,
            StorageForUrl(GURL("chrome-untrusted://print/")));
  EXPECT_EQ(PageStorage::kAny, StorageForUrl(GURL("about:blank")));
  EXPECT_EQ(PageStorage::kAny, StorageForUrl(GURL()));
}

TEST_F(ProfilePartitionTest, ANewTabOnAnotherProfileIsFixedToItsPartition) {
  scoped_refptr<content::SiteInstance> site_instance =
      SiteInstanceForProfile(&profile_, kWork, GURL("https://a.test/"));
  ASSERT_TRUE(site_instance);
  EXPECT_EQ(PartitionDomainForProfile(kWork),
            profile_.GetStoragePartition(site_instance.get())
                ->GetConfig()
                .partition_domain());
}

TEST_F(ProfilePartitionTest, ChromiumsChoiceStandsForDefaultAndBrowserPages) {
  EXPECT_FALSE(SiteInstanceForProfile(&profile_, DefaultProfileId(),
                                      GURL("https://a.test/")));
  EXPECT_FALSE(
      SiteInstanceForProfile(&profile_, kWork, GURL("chrome://settings/")));
  EXPECT_FALSE(SiteInstanceForProfile(
      profile_.GetPrimaryOTRProfile(/*create_if_needed=*/true), kWork,
      GURL("https://a.test/")));
  // A blank tab navigates nowhere yet, but will: it takes the profile.
  EXPECT_TRUE(SiteInstanceForProfile(&profile_, kWork, GURL("about:blank")));
}

TEST_F(ProfilePartitionTest, TheGuardsRule) {
  const std::string work = PartitionDomainForProfile(kWork);
  const GURL web("https://a.test/");
  const GURL settings("chrome://settings/");
  // A web page belongs in its space's profile.
  EXPECT_TRUE(IsInRightStorage(web, work, kWork));
  EXPECT_FALSE(IsInRightStorage(web, "", kWork));
  EXPECT_TRUE(IsInRightStorage(web, "", DefaultProfileId()));
  EXPECT_FALSE(IsInRightStorage(web, work, DefaultProfileId()));
  // A browser page belongs in shared storage, whatever the space.
  EXPECT_TRUE(IsInRightStorage(settings, "", kWork));
  EXPECT_FALSE(IsInRightStorage(settings, work, kWork));
  // A blank page holds nothing to leak.
  EXPECT_TRUE(IsInRightStorage(GURL("about:blank"), "", kWork));
  // A guest's or an app's partition is not Arcium's to judge.
  EXPECT_TRUE(IsInRightStorage(web, "abcdefghijklmnopabcdefghijklmnop", kWork));
}

}  // namespace
}  // namespace arcium
