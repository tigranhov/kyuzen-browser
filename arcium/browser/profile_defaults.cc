// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_defaults.h"

#include "base/values.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"

namespace arcium {

void SetProfileDefaults(user_prefs::PrefRegistrySyncable* registry) {
  // Continue where you left off. A Today tab has no home in the model file:
  // the session is the only record of it, so Chromium's default of a new tab
  // page would drop every one of them at every quit, and a split recorded
  // between two of them with it. R3.8 promises they come back; R3.9 keeps
  // that from costing a page load per tab.
  registry->SetDefaultPrefValue(
      prefs::kRestoreOnStartup,
      base::Value(SessionStartupPref::kPrefValueLast));
}

}  // namespace arcium
