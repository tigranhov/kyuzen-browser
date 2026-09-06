// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/sidebar_colors.h"

#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"

namespace arcium {

void AddArciumColorMixer(ui::ColorProvider* provider,
                         const ui::ColorProviderKey& key) {
  const bool dark = key.color_mode == ui::ColorProviderKey::ColorMode::kDark;
  ui::ColorMixer& mixer = provider->AddMixer();

  // Default space accent. Stage 3 makes this per space.
  constexpr SkColor kAccent = SkColorSetRGB(0x8A, 0x8A, 0xFF);
  const SkColor surface =
      dark ? SkColorSetRGB(0x17, 0x17, 0x1D) : SkColorSetRGB(0xF2, 0xF2, 0xF6);
  const SkColor text =
      dark ? SkColorSetRGB(0xC3, 0xC3, 0xCC) : SkColorSetRGB(0x33, 0x33, 0x3D);
  const SkColor ink = dark ? SK_ColorWHITE : SK_ColorBLACK;

  mixer[kColorArciumSidebarBackgroundBottom] = {surface};
  mixer[kColorArciumSidebarBackgroundTop] = {
      ui::AlphaBlend(kAccent, surface, dark ? 0x2E : 0x1A)};
  mixer[kColorArciumRowText] = {text};
  mixer[kColorArciumRowTextActive] = {
      dark ? SK_ColorWHITE : SkColorSetRGB(0x11, 0x11, 0x16)};
  mixer[kColorArciumRowTextSecondary] = {ui::SetAlpha(text, 0x99)};
  mixer[kColorArciumRowActiveBackground] = {
      ui::SetAlpha(ink, dark ? 0x1F : 0x14)};
  mixer[kColorArciumRowHoverBackground] = {
      ui::SetAlpha(ink, dark ? 0x10 : 0x0A)};
  mixer[kColorArciumControlBackground] = {
      ui::SetAlpha(ink, dark ? 0x14 : 0x0C)};
  mixer[kColorArciumControlIcon] = {ui::SetAlpha(text, 0xB3)};
  mixer[kColorArciumDivider] = {ui::SetAlpha(ink, 0x14)};
  mixer[kColorArciumSpaceAccent] = {kAccent};
  mixer[kColorArciumSpaceChipActiveBackground] = {ui::SetAlpha(kAccent, 0x38)};
}

}  // namespace arcium
