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

// True when the sidebar layout should be used for normal tabbed windows.
bool IsSidebarEnabled();

}  // namespace arcium::features

#endif  // ARCIUM_COMMON_ARCIUM_FEATURES_H_
