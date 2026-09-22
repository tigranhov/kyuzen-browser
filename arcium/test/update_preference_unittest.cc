// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/update_preference.h"

#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class UpdatePreferenceTest : public testing::Test {
 protected:
  UpdatePreferenceTest() { RegisterUpdatePrefs(prefs_.registry()); }

  TestingPrefServiceSimple* prefs() { return &prefs_; }

  TestingPrefServiceSimple prefs_;
};

TEST_F(UpdatePreferenceTest, AsksBeforeInstallingUntilTheReaderSaysOtherwise) {
  // A browser that replaces itself unasked is a surprise even when the new
  // version is wanted, so the default is the one that interrupts.
  EXPECT_EQ(UpdateMode::kAsk, GetUpdateMode(prefs()));
}

TEST_F(UpdatePreferenceTest, TheChosenValueIsTheOneItReadsBack) {
  SetUpdateMode(prefs(), UpdateMode::kAutomatic);
  EXPECT_EQ(UpdateMode::kAutomatic, GetUpdateMode(prefs()));
  SetUpdateMode(prefs(), UpdateMode::kOff);
  EXPECT_EQ(UpdateMode::kOff, GetUpdateMode(prefs()));
}

TEST_F(UpdatePreferenceTest, AValueThatIsNotOneOfTheThreeReadsAsAsking) {
  // The file can be edited by hand or written by an older build, and the
  // safe reading of a number nobody recognises is the one that asks first
  // rather than the one that installs without asking.
  prefs()->SetInteger(prefs::kUpdateMode, 47);
  EXPECT_EQ(UpdateMode::kAsk, GetUpdateMode(prefs()));
}

}  // namespace
}  // namespace arcium
