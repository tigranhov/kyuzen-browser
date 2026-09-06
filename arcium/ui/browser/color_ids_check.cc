// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "chrome/browser/ui/color/chrome_color_id.h"

// Arcium colour ids live above Chrome's; a collision would silently repaint
// some Chrome surface in an Arcium colour.
static_assert(arcium::kArciumColorsStart > kChromeColorsEnd,
              "Arcium colour ids overlap Chrome's");
