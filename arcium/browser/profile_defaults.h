// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_PROFILE_DEFAULTS_H_
#define ARCIUM_BROWSER_PROFILE_DEFAULTS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace arcium {

// Where Arcium's defaults for a profile's own settings differ from
// Chromium's. Called once Chromium has registered every profile setting
// (patch 0245), and changes defaults only: a choice made in settings is
// stored over the default and still wins.
void SetProfileDefaults(user_prefs::PrefRegistrySyncable* registry);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_PROFILE_DEFAULTS_H_
