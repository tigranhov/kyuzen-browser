// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_PROFILE_COLORS_H_
#define ARCIUM_UI_SIDEBAR_PROFILE_COLORS_H_

#include <string_view>

#include "base/containers/span.h"
#include "third_party/skia/include/core/SkColor.h"

namespace arcium {

// One colour a profile can choose: what its badge is filled with, and the
// name the colour menu lists it under. One value for both colour modes: the
// badge is a small solid disc, and every preset is a mid tone that reads on
// the light and the dark sidebar alike.
struct ProfileColor {
  SkColor color;
  std::u16string_view name;
};

// Every preset, in the order the colour menu lists them. A profile stores
// the index, never the colour, as a space stores its gradient
// (space_gradients.h). Preset 0 is the accent the badge drew before profiles
// existed, so Default looks as it always did.
base::span<const ProfileColor> ProfileColors();

// The preset `index` names, or preset 0 for an index outside the table: a
// file written by a later build with a longer palette still draws.
const ProfileColor& ProfileColorAt(int index);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_PROFILE_COLORS_H_
