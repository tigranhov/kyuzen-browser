// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/test/test_app_activation.h"

#import <AppKit/AppKit.h>
#include <objc/runtime.h>

namespace arcium::test {
namespace {

// Swallows the promotion. Returning NO reports "the policy did not change",
// which is the truth: the application stays at the unbundled default.
BOOL IgnoreActivationPolicy(id self,
                            SEL selector,
                            NSApplicationActivationPolicy policy) {
  return NO;
}

}  // namespace

void SuppressTestAppActivation() {
  // Idempotent: two fixtures call this, and each calls it once per test.
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;

  // `NSApp.activationPolicy = ...` compiles to this selector, so replacing its
  // implementation is what stops the promotion rather than undoing it after.
  Method method = class_getInstanceMethod([NSApplication class],
                                          @selector(setActivationPolicy:));
  method_setImplementation(method,
                           reinterpret_cast<IMP>(&IgnoreActivationPolicy));
}

}  // namespace arcium::test
