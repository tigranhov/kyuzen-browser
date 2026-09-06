// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/common/branding_hooks.h"

namespace arcium {

bool ShouldShowGoogleApiKeysInfoBar() {
  return false;
}

bool IsSigninAllowed() {
  return false;
}

std::string UserAgentBrand() {
  return "Google Chrome";
}

}  // namespace arcium
