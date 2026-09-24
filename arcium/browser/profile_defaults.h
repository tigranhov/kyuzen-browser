// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_PROFILE_DEFAULTS_H_
#define ARCIUM_BROWSER_PROFILE_DEFAULTS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace arcium {

// How far the welcome has got on this profile: not started, a step reached
// (WelcomeStep's value plus one, so a quit brings it back there), or done,
// which covers both finished and skipped. See arcium/ui/welcome/.
inline constexpr char kWelcomeStepPref[] = "arcium.welcome_step";
inline constexpr int kWelcomeNotStarted = 0;
inline constexpr int kWelcomeDone = 100;

// Arcium's part of a profile's settings: the defaults it holds that differ
// from Chromium's, and the default of the one setting it adds, the welcome's
// progress. Called once Chromium has registered every profile setting (patch
// 0245), so a default it changes is already there to change; a choice made in
// settings is stored over the default and still wins.
void SetProfileDefaults(user_prefs::PrefRegistrySyncable* registry);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_PROFILE_DEFAULTS_H_
