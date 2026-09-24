// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_WELCOME_WELCOME_STYLE_H_
#define ARCIUM_UI_WELCOME_WELCOME_STYLE_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/color/color_id.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace gfx {
struct VectorIcon;
}

namespace arcium::welcome {

// The card's measurements, from the approved mockups
// (docs/superpowers/specs/2026-09-24-welcome-mockups/).
inline constexpr int kCardWidth = 840;
inline constexpr int kCardHeight = 532;
inline constexpr int kCardRadius = 14;
inline constexpr int kCardPadding = 28;
inline constexpr int kPanelWidth = 318;
// The panel narrows with the card down to this, and then goes.
inline constexpr int kPanelMinWidth = 288;
inline constexpr int kPanelRadius = 12;
inline constexpr int kTileRadius = 10;
inline constexpr int kFooterHeight = 70;
// Narrower than this, the card drops its right-hand panel rather than
// squeezing the choices.
inline constexpr int kPanelMinCardWidth = 760;

enum class TextStyle {
  // "STEP 1 OF 6".
  kEyebrow,
  kTitle,
  kSubtitle,
  kPanelTitle,
  kBody,
  kBodyStrong,
  kCaption,
};

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        TextStyle style);

// A rounded box with a hairline edge: every tile and row on the card. A
// highlighted one is edged in the accent, for the choice that is made.
std::unique_ptr<views::View> MakeTile(bool highlighted = false);

// A square holding an emoji, or the name's first letter when there is none:
// how the sidebar draws a space or a page it has no icon for.
std::unique_ptr<views::View> MakeBadge(const std::u16string& icon,
                                       const std::u16string& name,
                                       int size);

std::unique_ptr<views::ImageView> MakeIcon(const gfx::VectorIcon& icon,
                                           int size,
                                           ui::ColorId color);

// A key as a keyboard draws it.
std::unique_ptr<views::View> MakeKeycap(const std::u16string& key);

// "1 pinned tab", "12 pinned tabs".
std::u16string CountOf(size_t count,
                       std::u16string_view one,
                       std::u16string_view many);

// A row with a badge on the left and two lines beside it: a space and its
// pinned count, a space and where it came from.
std::unique_ptr<views::View> MakeSpaceRow(const std::u16string& icon,
                                          const std::u16string& name,
                                          const std::u16string& detail);

// A horizontal hairline.
std::unique_ptr<views::View> MakeRule();

// Chromium's checkbox and radio button tick in its own blue, and take the
// colour only as a value, so these look it up again whenever the theme
// changes and tick in the accent.
class AccentCheckbox : public views::Checkbox {
  METADATA_HEADER(AccentCheckbox, views::Checkbox)

 public:
  AccentCheckbox();
  ~AccentCheckbox() override;

  // views::Checkbox:
  void OnThemeChanged() override;
};

class AccentRadioButton : public views::RadioButton {
  METADATA_HEADER(AccentRadioButton, views::RadioButton)

 public:
  AccentRadioButton(const std::u16string& label, int group);
  ~AccentRadioButton() override;

  // views::RadioButton:
  void OnThemeChanged() override;
};

}  // namespace arcium::welcome

#endif  // ARCIUM_UI_WELCOME_WELCOME_STYLE_H_
