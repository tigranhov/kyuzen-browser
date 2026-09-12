// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/profile_colors.h"

namespace arcium {

namespace {

constexpr ProfileColor kColors[] = {
    {.color = SkColorSetRGB(0x8A, 0x8A, 0xFF), .name = u"Lavender"},
    {.color = SkColorSetRGB(0x4C, 0x8D, 0xF6), .name = u"Blue"},
    {.color = SkColorSetRGB(0x2F, 0xB5, 0x8A), .name = u"Green"},
    {.color = SkColorSetRGB(0xE8, 0xA3, 0x3D), .name = u"Amber"},
    {.color = SkColorSetRGB(0xE5, 0x5B, 0x5B), .name = u"Red"},
    {.color = SkColorSetRGB(0xD9, 0x6C, 0xC4), .name = u"Pink"},
    {.color = SkColorSetRGB(0x2C, 0xAE, 0xC4), .name = u"Teal"},
    {.color = SkColorSetRGB(0x8C, 0x93, 0x9E), .name = u"Grey"},
};

}  // namespace

base::span<const ProfileColor> ProfileColors() {
  return kColors;
}

const ProfileColor& ProfileColorAt(int index) {
  const base::span<const ProfileColor> colors = ProfileColors();
  if (index < 0 || static_cast<size_t>(index) >= colors.size()) {
    return colors[0];
  }
  return colors[static_cast<size_t>(index)];
}

}  // namespace arcium
