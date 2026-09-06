// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_COMMON_ARCIUM_FEATURES_H_
#define ARCIUM_COMMON_ARCIUM_FEATURES_H_

#include "base/feature_list.h"

namespace arcium::features {

// The sidebar-first window layout. When disabled the window is stock Chromium.
BASE_DECLARE_FEATURE(kArciumSidebar);

// Command line switch that turns the sidebar off for one run, for debugging.
inline constexpr char kNoSidebarSwitch[] = "arcium-no-sidebar";

// Debugging: --arcium-snapshot=<file.png> paints the first browser window's
// Views (not the page) to a PNG a few seconds after it opens, then continues.
// Screen capture needs a macOS permission that automated runs do not have.
inline constexpr char kSnapshotSwitch[] = "arcium-snapshot";
// Seconds to wait before the snapshot (default 4), so a script can set up
// tabs over DevTools first.
inline constexpr char kSnapshotDelaySwitch[] = "arcium-snapshot-delay";

// Debugging: --arcium-quick-entry opens the Cmd+T quick entry once the window
// is up, so the bubble can be exercised where synthetic key presses are not
// available (macOS withholds them from automated sessions).
inline constexpr char kQuickEntrySwitch[] = "arcium-quick-entry";

// True when the sidebar layout should be used for normal tabbed windows.
bool IsSidebarEnabled();

// macOS immersive fullscreen moves top chrome into a separate overlay window.
// Arcium hides the tab strip and toolbar, so that overlay would be zero-sized,
// which is unsupported (it DCHECKs). Plain fullscreen keeps the sidebar and
// gives the page the rest of the screen.
bool UsesImmersiveFullscreen();

}  // namespace arcium::features

#endif  // ARCIUM_COMMON_ARCIUM_FEATURES_H_
