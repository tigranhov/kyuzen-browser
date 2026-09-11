// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SPACE_GRADIENTS_H_
#define ARCIUM_UI_SIDEBAR_SPACE_GRADIENTS_H_

#include <string_view>

#include "base/containers/span.h"
#include "third_party/skia/include/core/SkColor.h"

namespace arcium {

// One colour preset a space can choose for its sidebar: the two ends of the
// tint in each colour mode, and the name the theme menu lists it under.
struct SpaceGradient {
  SkColor light_top;
  SkColor light_bottom;
  SkColor dark_top;
  SkColor dark_bottom;
  std::u16string_view name;
};

// Every preset, in the order the theme menu lists them. A space stores the
// index, never the colours, so this table is the one thing the menu, the
// stored index and the painted tint agree on -- its size included. Preset 0
// is not a pair of its own but the colour mixer's; see the .cc.
base::span<const SpaceGradient> SpaceGradients();

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SPACE_GRADIENTS_H_
