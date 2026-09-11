// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/space_gradients.h"

namespace arcium {

namespace {

// Preset 0 is not a colour: it means "whatever the colour mixer says", which
// is the pair the sidebar has always drawn. A space that never chose a
// gradient therefore looks exactly as it did before spaces existed, and the
// snapshot tests that predate this file keep passing.
//
// The others stay close to the mixer's surface at the bottom and carry their
// hue at the top, so the row text the mixer chose reads on every one of them.
constexpr SpaceGradient kGradients[] = {
    {.light_top = SK_ColorTRANSPARENT,
     .light_bottom = SK_ColorTRANSPARENT,
     .dark_top = SK_ColorTRANSPARENT,
     .dark_bottom = SK_ColorTRANSPARENT,
     .name = u"Default"},
    {.light_top = SkColorSetRGB(0xF6, 0xD9, 0xE1),
     .light_bottom = SkColorSetRGB(0xF4, 0xEE, 0xF1),
     .dark_top = SkColorSetRGB(0x3A, 0x1F, 0x2A),
     .dark_bottom = SkColorSetRGB(0x1C, 0x15, 0x19),
     .name = u"Rose"},
    {.light_top = SkColorSetRGB(0xF7, 0xE6, 0xC8),
     .light_bottom = SkColorSetRGB(0xF5, 0xF0, 0xE8),
     .dark_top = SkColorSetRGB(0x3A, 0x2E, 0x18),
     .dark_bottom = SkColorSetRGB(0x1C, 0x19, 0x14),
     .name = u"Amber"},
    {.light_top = SkColorSetRGB(0xD3, 0xF0, 0xE3),
     .light_bottom = SkColorSetRGB(0xEE, 0xF4, 0xF1),
     .dark_top = SkColorSetRGB(0x1A, 0x34, 0x29),
     .dark_bottom = SkColorSetRGB(0x14, 0x1B, 0x18),
     .name = u"Mint"},
    {.light_top = SkColorSetRGB(0xD4, 0xE6, 0xF7),
     .light_bottom = SkColorSetRGB(0xEE, 0xF2, 0xF6),
     .dark_top = SkColorSetRGB(0x18, 0x2C, 0x40),
     .dark_bottom = SkColorSetRGB(0x13, 0x17, 0x1D),
     .name = u"Ocean"},
    {.light_top = SkColorSetRGB(0xE2, 0xD8, 0xF7),
     .light_bottom = SkColorSetRGB(0xF1, 0xEE, 0xF6),
     .dark_top = SkColorSetRGB(0x2A, 0x1F, 0x42),
     .dark_bottom = SkColorSetRGB(0x17, 0x15, 0x1D),
     .name = u"Violet"},
    {.light_top = SkColorSetRGB(0xDD, 0xE1, 0xE6),
     .light_bottom = SkColorSetRGB(0xF0, 0xF1, 0xF3),
     .dark_top = SkColorSetRGB(0x26, 0x2A, 0x30),
     .dark_bottom = SkColorSetRGB(0x16, 0x17, 0x1A),
     .name = u"Slate"},
    {.light_top = SkColorSetRGB(0xF7, 0xD6, 0xCC),
     .light_bottom = SkColorSetRGB(0xF3, 0xE6, 0xEE),
     .dark_top = SkColorSetRGB(0x3D, 0x20, 0x19),
     .dark_bottom = SkColorSetRGB(0x1E, 0x15, 0x20),
     .name = u"Sunset"},
};

}  // namespace

base::span<const SpaceGradient> SpaceGradients() {
  return kGradients;
}

}  // namespace arcium
