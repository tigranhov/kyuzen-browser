// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_UPDATE_UPDATE_PREFERENCE_H_
#define ARCIUM_BROWSER_UPDATE_UPDATE_PREFERENCE_H_

class PrefRegistrySimple;
class PrefService;

namespace arcium {

// How a new version of the browser is allowed to arrive. One per
// installation rather than one per profile, because there is one application
// on disk and every profile runs from it.
enum class UpdateMode {
  // Look every day and offer what is found. The default, because replacing
  // the browser is a thing a reader should get to refuse.
  kAsk = 0,
  // Look every day, download without asking, and install on the next quit.
  kAutomatic = 1,
  // Never look. Asking for a check by hand still works.
  kOff = 2,
};

namespace prefs {
inline constexpr char kUpdateMode[] = "arcium.update_mode";
}  // namespace prefs

void RegisterUpdatePrefs(PrefRegistrySimple* registry);

// Anything stored that is not one of the three reads as asking: the value can
// come from a file edited by hand or written by a build that knew a different
// set, and the safe reading of a number nobody recognises is the one that
// interrupts rather than the one that installs unasked.
UpdateMode GetUpdateMode(const PrefService* prefs);

void SetUpdateMode(PrefService* prefs, UpdateMode mode);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_UPDATE_UPDATE_PREFERENCE_H_
