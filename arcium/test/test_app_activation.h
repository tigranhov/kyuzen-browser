// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_TEST_APP_ACTIVATION_H_
#define ARCIUM_TEST_TEST_APP_ACTIVATION_H_

namespace arcium::test {

// Stops a Views test binary from stealing the developer's focus.
//
// `ViewsTestHelperMac`'s constructor promotes the unbundled test binary to
// `NSApplicationActivationPolicyRegular`
// (ui/views/test/views_test_helper_mac.mm), and that constructor runs once per
// test. A foreground application that shows and activates windows pulls the
// desktop onto its Space, so a suite does it hundreds of times and the machine
// is unusable until the run ends.
//
// Undoing the promotion afterwards is worse than leaving it: demoting after
// each test turns one policy transition per process into one per test, and
// every transition is its own focus event and its own Dock registration.
// So this suppresses the promotion instead, by replacing the implementation of
// `-[NSApplication setActivationPolicy:]` with one that does nothing. The
// binary stays at the unbundled default of `Prohibited`, which — in upstream's
// own words — prohibits the application from obtaining key status or
// activating windows without user interaction.
//
// Nothing in these tests reads real activation. `ui_controls` is disabled here,
// so `ScopedFakeNSWindowFocus` swizzles key and main status and
// `Widget::IsActive()` reads the swizzled answer rather than the window
// server's.
//
// Call from a fixture's SetUp **before** `ViewsTestBase::SetUp()`, so the
// suppression is in place before the helper is constructed. Idempotent, and a
// no-op away from macOS.
void SuppressTestAppActivation();

}  // namespace arcium::test

#endif  // ARCIUM_TEST_TEST_APP_ACTIVATION_H_
