// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_COMMON_BRANDING_HOOKS_H_
#define ARCIUM_COMMON_BRANDING_HOOKS_H_

#include <string>

// Decisions that upstream Chromium makes from its own branding, overridden for
// Arcium. Each function is called from exactly one hook patch.
namespace arcium {

// Arcium ships without Google API keys on purpose; the infobar is noise.
bool ShouldShowGoogleApiKeysInfoBar();

// Google account sign-in cannot work without OAuth keys; saying so up front
// hides the sign-in and sync controls in Settings.
bool IsSigninAllowed();

// Brand reported in user-agent client hints. "Google Chrome" makes the Chrome
// Web Store treat Arcium as Chrome (spec D6, decision 2026-09-06).
std::string UserAgentBrand();

}  // namespace arcium

#endif  // ARCIUM_COMMON_BRANDING_HOOKS_H_
