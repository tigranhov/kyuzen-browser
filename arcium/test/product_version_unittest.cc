// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/common/product_version.h"

#include <string>

#include "base/version.h"
#include "components/version_info/version_info.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

TEST(ProductVersionTest, TheProductHasAVersionOfItsOwn) {
  // Not Chromium's, because two releases of this browser can carry the same
  // Chromium and nothing would then tell them apart -- which is what an
  // update has to decide before it replaces anything.
  EXPECT_NE(std::string(version_info::GetVersionNumber()), ProductVersion());
}

TEST(ProductVersionTest, TheVersionReadsAsAVersion) {
  const base::Version version(ProductVersion());
  EXPECT_TRUE(version.IsValid()) << ProductVersion() << " is not a version";
  ASSERT_EQ(3u, version.components().size())
      << "three parts, so that a release can be a patch of the one before it";
}

TEST(ProductVersionTest, TheBuildNumberCounts) {
  // The one number an update compares. Zero would mean "older than every
  // build", including itself.
  EXPECT_GE(BuildNumber(), 1);
}

}  // namespace
}  // namespace arcium
