// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/update_preference.h"

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace arcium {

void RegisterUpdatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterIntegerPref(prefs::kUpdateMode,
                                static_cast<int>(UpdateMode::kAsk));
}

UpdateMode GetUpdateMode(const PrefService* prefs) {
  switch (prefs->GetInteger(prefs::kUpdateMode)) {
    case static_cast<int>(UpdateMode::kAutomatic):
      return UpdateMode::kAutomatic;
    case static_cast<int>(UpdateMode::kOff):
      return UpdateMode::kOff;
    default:
      return UpdateMode::kAsk;
  }
}

void SetUpdateMode(PrefService* prefs, UpdateMode mode) {
  prefs->SetInteger(prefs::kUpdateMode, static_cast<int>(mode));
}

}  // namespace arcium
