// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/sidebar_colors.h"

#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
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
  mixer[kColorArciumRowTextActive] = {dark ? SK_ColorWHITE
                                           : SkColorSetRGB(0x11, 0x11, 0x16)};
  mixer[kColorArciumRowTextSecondary] = {ui::SetAlpha(text, 0x99)};
  // Blended toward the sidebar surface rather than just given a lower alpha,
  // the way kColorArciumSidebarBackgroundTop mixes its accent: row text sits
  // on a surface that already carries the gradient tint, and alpha alone
  // would let that tint show through unevenly between the two tones. Half the
  // row text's weight reads as closed beside a loaded row without fading to
  // the point of being unreadable in either theme.
  mixer[kColorArciumRowTextUnloaded] = {ui::AlphaBlend(text, surface, 0x80)};
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

  // The welcome card, from the approved mockups: a page a shade darker (or
  // lighter) than the card, so the card reads as the one thing on it, and a
  // violet accent that sits beside the sidebar's own without matching it.
  const SkColor welcome_accent =
      dark ? SkColorSetRGB(0x7C, 0x6D, 0xF6) : SkColorSetRGB(0x5B, 0x4C, 0xEB);
  mixer[kColorArciumWelcomeBackground] = {
      dark ? SkColorSetRGB(0x15, 0x15, 0x1B) : SkColorSetRGB(0xF4, 0xF4, 0xF8)};
  mixer[kColorArciumWelcomeCard] = {dark ? SkColorSetRGB(0x1E, 0x1E, 0x25)
                                         : SK_ColorWHITE};
  mixer[kColorArciumWelcomeCardBorder] = {
      dark ? SkColorSetRGB(0x2E, 0x2E, 0x37) : SkColorSetRGB(0xE4, 0xE4, 0xEC)};
  mixer[kColorArciumWelcomePanel] = {dark ? SkColorSetRGB(0x25, 0x25, 0x2D)
                                          : SkColorSetRGB(0xF3, 0xF3, 0xF7)};
  mixer[kColorArciumWelcomeTile] = {dark ? SkColorSetRGB(0x22, 0x22, 0x2A)
                                         : SK_ColorWHITE};
  mixer[kColorArciumWelcomeTileBorder] = {
      dark ? SkColorSetRGB(0x34, 0x34, 0x3D) : SkColorSetRGB(0xE2, 0xE2, 0xEA)};
  mixer[kColorArciumWelcomeText] = {dark ? SkColorSetRGB(0xF2, 0xF2, 0xF5)
                                         : SkColorSetRGB(0x16, 0x16, 0x1C)};
  mixer[kColorArciumWelcomeTextSecondary] = {
      dark ? SkColorSetRGB(0xA0, 0xA0, 0xAC) : SkColorSetRGB(0x6B, 0x6B, 0x78)};
  mixer[kColorArciumWelcomeAccent] = {welcome_accent};
  mixer[kColorArciumWelcomeAccentSoft] = {
      ui::SetAlpha(welcome_accent, dark ? 0x33 : 0x1F)};
  mixer[kColorArciumWelcomeOnAccent] = {SK_ColorWHITE};
  mixer[kColorArciumWelcomeDotIdle] = {dark ? SkColorSetRGB(0x4A, 0x4A, 0x55)
                                            : SkColorSetRGB(0xCF, 0xCF, 0xD9)};

  // The window frame, in the sidebar's own colour. Arcium hides the toolbar,
  // so the frame is not a title bar here -- it is whatever window shows
  // behind the page, and left at Chromium's default it reads as a pale line
  // across the top of every tab. The page now covers it, so this shows only
  // while the window is being resized or a tab has yet to paint; the top
  // tint rather than the bottom one, because what it borders there is the
  // sidebar's own lightest end.
  mixer[ui::kColorFrameActive] = {kColorArciumSidebarBackgroundTop};
  mixer[ui::kColorFrameInactive] = {kColorArciumSidebarBackgroundTop};
}

}  // namespace arcium
