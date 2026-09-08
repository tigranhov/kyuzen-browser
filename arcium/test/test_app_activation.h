// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_TEST_APP_ACTIVATION_H_
#define ARCIUM_TEST_TEST_APP_ACTIVATION_H_

namespace arcium::test {

// Stops a Views test binary from stealing the developer's focus.
//
// `ViewsTestHelperMac` promotes the unbundled test binary to
// `NSApplicationActivationPolicyRegular`
// (ui/views/test/views_test_helper_mac.mm), deliberately, because the default
// `Prohibited` — in that file's own words — prohibits an application from
// obtaining key status or activating windows without user interaction. A
// regular application that shows and activates windows pulls the desktop to
// whatever Space it is on, which makes a machine unusable for as long as the
// suite runs.
//
// Arcium's fixtures do not need the promotion. Focus is already faked:
// `ui_controls` is disabled in these tests, so `ScopedFakeNSWindowFocus`
// swizzles key and main status, and `Widget::IsActive()` reads the swizzled
// answer rather than the window server's. Dropping back to `Accessory` — an
// application with no Dock tile and no menu bar, which does not activate on
// showing a window — therefore changes what the desktop does, not what the
// tests observe.
//
// Call once from a fixture's SetUp, after `ViewsTestBase::SetUp()` has run and
// made the promotion. A no-op away from macOS.
void SuppressTestAppActivation();

}  // namespace arcium::test

#endif  // ARCIUM_TEST_TEST_APP_ACTIVATION_H_
