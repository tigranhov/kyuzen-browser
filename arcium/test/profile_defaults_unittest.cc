// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_defaults.h"

#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class ProfileDefaultsTest : public testing::Test {
 protected:
  ProfileDefaultsTest() {
    SessionStartupPref::RegisterProfilePrefs(prefs_.registry());
  }

  SessionStartupPref::Type StartupType() const {
    return SessionStartupPref::GetStartupPref(&prefs_).type;
  }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

// Today tabs have no home but the session, so a profile that opened a new
// tab page at launch would lose every one of them at every quit.
TEST_F(ProfileDefaultsTest, AProfileNobodyChangedContinuesWhereItLeftOff) {
  // The control: Chromium's own default is a new tab page, so the assertion
  // below is about Arcium's default and not about Chromium's.
  ASSERT_NE(SessionStartupPref::LAST, StartupType());

  SetProfileDefaults(prefs_.registry());

  EXPECT_EQ(SessionStartupPref::LAST, StartupType());
  // A default rather than a stored choice, so the setting still reads as
  // untouched and a later change to the default reaches this profile too.
  EXPECT_TRUE(
      prefs_.FindPreference(prefs::kRestoreOnStartup)->IsDefaultValue());
}

TEST_F(ProfileDefaultsTest, AChoiceMadeInSettingsStillWins) {
  SetProfileDefaults(prefs_.registry());

  SessionStartupPref::SetStartupPref(
      &prefs_, SessionStartupPref(SessionStartupPref::DEFAULT));

  EXPECT_EQ(SessionStartupPref::DEFAULT, StartupType());
}

// The welcome's progress starts unset, which is what a profile nobody has
// shown it to reads as.
TEST_F(ProfileDefaultsTest, TheWelcomeStartsNotStarted) {
  SetProfileDefaults(prefs_.registry());

  EXPECT_EQ(kWelcomeNotStarted, prefs_.GetInteger(kWelcomeStepPref));
}

}  // namespace
}  // namespace arcium
