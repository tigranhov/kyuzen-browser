// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/test/test_app_activation.h"

#import <AppKit/AppKit.h>

namespace arcium::test {

void SuppressTestAppActivation() {
  // Accessory, not Prohibited: AppKit supports leaving Regular for Accessory,
  // while a move back to Prohibited is documented as one-way and returns NO.
  // Accessory is enough — it is the policy with no Dock tile and no menu bar,
  // and it does not bring the application forward when a window is shown.
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

}  // namespace arcium::test
