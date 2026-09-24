// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_COLORS_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_COLORS_H_

#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"

namespace ui {
struct ColorProviderKey;
}

namespace arcium {

// Colour ids for the sidebar, allocated after Chrome's own range so both can
// live in one ColorProvider. The value must not collide with
// chrome/browser/ui/color/chrome_color_id.h; kChromeColorsEnd is the floor.
enum ArciumColorIds : ui::ColorId {
  kArciumColorsStart = 0x7A000,  // Well above every Chrome and component id.
  kColorArciumSidebarBackgroundTop = kArciumColorsStart,
  kColorArciumSidebarBackgroundBottom,
  kColorArciumRowText,
  kColorArciumRowTextActive,
  kColorArciumRowTextSecondary,
  // A row whose click has to load a page first -- a closed entry, or a tab
  // whose page is not in memory. Distinct from kColorArciumRowTextSecondary,
  // which marks auxiliary text (counts, timestamps) rather than a row's own
  // state: the two are free to diverge later without one dragging the other
  // along.
  kColorArciumRowTextUnloaded,
  kColorArciumRowActiveBackground,
  kColorArciumRowHoverBackground,
  kColorArciumControlBackground,
  kColorArciumControlIcon,
  kColorArciumDivider,
  kColorArciumSpaceAccent,
  kColorArciumSpaceChipActiveBackground,
  // The welcome card (arcium/ui/welcome/): the page area around it, the card
  // and its right-hand panel, the tiles and rows on it, its two weights of
  // text, and the accent its choices and its Continue button are drawn in.
  kColorArciumWelcomeBackground,
  kColorArciumWelcomeCard,
  kColorArciumWelcomeCardBorder,
  kColorArciumWelcomePanel,
  kColorArciumWelcomeTile,
  kColorArciumWelcomeTileBorder,
  kColorArciumWelcomeText,
  kColorArciumWelcomeTextSecondary,
  kColorArciumWelcomeAccent,
  kColorArciumWelcomeAccentSoft,
  kColorArciumWelcomeOnAccent,
  kColorArciumWelcomeDotIdle,
  kArciumColorsEnd,
};

// Adds the Arcium colours to `provider`. Registered by the browser through a
// hook and by the playground directly.
void AddArciumColorMixer(ui::ColorProvider* provider,
                         const ui::ColorProviderKey& key);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_COLORS_H_
