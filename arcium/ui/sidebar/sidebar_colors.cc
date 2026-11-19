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

  // The sidebar's own accent, which also tints its top. Drop marks and focus
  // rings take kColorArciumSpaceAccent instead, which deepens in light so
  // they keep 3:1 on the light surface.
  constexpr SkColor kAccent = SkColorSetRGB(0x8A, 0x8A, 0xFF);
  const SkColor marks = dark ? kAccent : SkColorSetRGB(0x6A, 0x5C, 0xF2);
  const SkColor surface =
      dark ? SkColorSetRGB(0x17, 0x17, 0x1D) : SkColorSetRGB(0xF2, 0xF2, 0xF6);
  const SkColor text =
      dark ? SkColorSetRGB(0xC3, 0xC3, 0xCC) : SkColorSetRGB(0x33, 0x33, 0x3D);
  // Secondary text: counts, addresses, labels, and rows not yet loaded. A
  // solid tone rather than row text at an alpha, so it reads the same on
  // every space gradient (4.8:1 on the dark top tint).
  const SkColor text_tertiary =
      dark ? SkColorSetRGB(0x9A, 0x9A, 0xA6) : SkColorSetRGB(0x6E, 0x6E, 0x7A);
  const SkColor ink = dark ? SK_ColorWHITE : SK_ColorBLACK;
  // The four fill steps of the state ladder, as ink over whatever gradient
  // the space has, so no theme needs a token of its own.
  const SkAlpha fill1 = dark ? 0x0D : 0x0A;  // 5% (4%)
  const SkAlpha fill2 = dark ? 0x14 : 0x0F;  // 8% (6%)
  const SkAlpha fill3 = dark ? 0x1F : 0x17;  // 12% (9%)
  const SkAlpha fill4 = dark ? 0x29 : 0x1F;  // 16% (12%)

  mixer[kColorArciumSidebarBackgroundBottom] = {surface};
  mixer[kColorArciumSidebarBackgroundTop] = {
      ui::AlphaBlend(kAccent, surface, dark ? 0x2E : 0x1A)};
  mixer[kColorArciumRowText] = {text};
  mixer[kColorArciumRowTextActive] = {dark ? SkColorSetRGB(0xF2, 0xF2, 0xF5)
                                           : SkColorSetRGB(0x16, 0x16, 0x1C)};
  mixer[kColorArciumRowTextSecondary] = {text_tertiary};
  // The same tone as secondary text, so a closed row stays readable; what
  // marks it as closed is the dimmed favicon beside it, not a fainter title.
  mixer[kColorArciumRowTextUnloaded] = {text_tertiary};
  mixer[kColorArciumRowHoverBackground] = {ui::SetAlpha(ink, fill1)};
  mixer[kColorArciumRowPressedBackground] = {ui::SetAlpha(ink, fill2)};
  mixer[kColorArciumRowActiveBackground] = {ui::SetAlpha(ink, fill3)};
  mixer[kColorArciumRowActiveHoverBackground] = {ui::SetAlpha(ink, fill4)};
  mixer[kColorArciumControlBackground] = {ui::SetAlpha(ink, fill1)};
  mixer[kColorArciumControlHoverBackground] = {ui::SetAlpha(ink, fill2)};
  mixer[kColorArciumControlIcon] = {ui::SetAlpha(text, 0xB3)};
  mixer[kColorArciumDivider] = {ui::SetAlpha(ink, 0x14)};
  mixer[kColorArciumSpaceAccent] = {marks};
  mixer[kColorArciumSpaceChipActiveBackground] = {ui::SetAlpha(marks, 0x33)};
  mixer[kColorArciumFocusRing] = {marks};
  mixer[kColorArciumSurfaceRaised] = {dark ? SkColorSetRGB(0x1E, 0x1E, 0x25)
                                           : SK_ColorWHITE};

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
