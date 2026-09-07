// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/common/arcium_features.h"

#include "base/command_line.h"
#include "base/test/scoped_command_line.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium::features {
namespace {

// The switch is read from the process command line on every call rather than
// cached, so a test can set it and ask. It is read once per window in
// production, which is not a place where parsing a short string matters.
base::TimeDelta OffsetFor(const char* value) {
  base::test::ScopedCommandLine scoped;
  scoped.GetProcessCommandLine()->AppendSwitchASCII(kFakeClockOffsetSwitch,
                                                    value);
  return FakeClockOffset();
}

TEST(ArciumFakeClockOffsetTest, AbsentSwitchMeansNoOffset) {
  base::test::ScopedCommandLine scoped;
  EXPECT_TRUE(FakeClockOffset().is_zero());
}

TEST(ArciumFakeClockOffsetTest, ADurationIsParsed) {
  EXPECT_EQ(base::Hours(13), OffsetFor("13h"));
  EXPECT_EQ(base::Days(2), OffsetFor("2d"));
  EXPECT_EQ(base::Minutes(90), OffsetFor("1h30m"));
}

TEST(ArciumFakeClockOffsetTest, AMalformedValueMeansNoOffset) {
  EXPECT_TRUE(OffsetFor("thirteen hours").is_zero());
  EXPECT_TRUE(OffsetFor("").is_zero());
  EXPECT_TRUE(OffsetFor("13").is_zero());
}

// The switch means "pretend this much time has passed". Nothing sensible
// happens when it is asked to run the archive backwards or to infinity, and
// an infinite offset would saturate every comparison it takes part in.
TEST(ArciumFakeClockOffsetTest, ABackwardsOrInfiniteOffsetIsRefused) {
  EXPECT_TRUE(OffsetFor("-13h").is_zero());
  EXPECT_TRUE(OffsetFor("0").is_zero());
  EXPECT_TRUE(OffsetFor("inf").is_zero());
}

}  // namespace
}  // namespace arcium::features
