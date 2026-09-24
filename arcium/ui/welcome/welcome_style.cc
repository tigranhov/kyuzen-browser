// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/welcome/welcome_style.h"

#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/third_party/icu/icu_utf.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"

namespace arcium::welcome {
namespace {

struct Face {
  int size_delta;
  gfx::Font::Weight weight;
  bool secondary;
};

Face FaceFor(TextStyle style) {
  switch (style) {
    case TextStyle::kEyebrow:
      return {-2, gfx::Font::Weight::SEMIBOLD, true};
    case TextStyle::kTitle:
      return {17, gfx::Font::Weight::BOLD, false};
    case TextStyle::kSubtitle:
      return {2, gfx::Font::Weight::NORMAL, true};
    case TextStyle::kPanelTitle:
      return {3, gfx::Font::Weight::SEMIBOLD, false};
    case TextStyle::kBody:
      return {0, gfx::Font::Weight::NORMAL, false};
    case TextStyle::kBodyStrong:
      return {1, gfx::Font::Weight::SEMIBOLD, false};
    case TextStyle::kCaption:
      return {-1, gfx::Font::Weight::NORMAL, true};
  }
}

// The name's first letter, as the sidebar's space chips draw it: a letter
// outside the Basic Multilingual Plane is two UTF-16 units, and half of one
// draws as a box.
std::u16string FirstLetter(const std::u16string& name) {
  if (name.empty()) {
    return u"?";
  }
  const size_t length = CBU16_IS_LEAD(name[0]) && name.size() > 1 ? 2 : 1;
  return base::ToUpperASCII(name.substr(0, length));
}

}  // namespace

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        TextStyle style) {
  const Face face = FaceFor(style);
  auto label = std::make_unique<views::Label>(
      text, views::Label::CustomFont{gfx::FontList()
                                         .DeriveWithSizeDelta(face.size_delta)
                                         .DeriveWithWeight(face.weight)});
  label->SetEnabledColor(face.secondary ? kColorArciumWelcomeTextSecondary
                                        : kColorArciumWelcomeText);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetAutoColorReadabilityEnabled(false);
  return label;
}

std::unique_ptr<views::View> MakeTile(bool highlighted) {
  auto tile = std::make_unique<views::View>();
  tile->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumWelcomeTile, kTileRadius, /*for_border_thickness=*/1));
  tile->SetBorder(views::CreateRoundedRectBorder(
      1, kTileRadius,
      highlighted ? kColorArciumWelcomeAccent : kColorArciumWelcomeTileBorder));
  return tile;
}

std::unique_ptr<views::View> MakeBadge(const std::u16string& icon,
                                       const std::u16string& name,
                                       int size) {
  auto badge = std::make_unique<views::View>();
  badge->SetLayoutManager(std::make_unique<views::FillLayout>());
  badge->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumWelcomePanel, size / 4.0f));
  badge->SetPreferredSize(gfx::Size(size, size));
  auto label = MakeLabel(icon.empty() ? FirstLetter(name) : icon,
                         TextStyle::kBodyStrong);
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  badge->AddChildView(std::move(label));
  return badge;
}

std::unique_ptr<views::ImageView> MakeIcon(const gfx::VectorIcon& icon,
                                           int size,
                                           ui::ColorId color) {
  return std::make_unique<views::ImageView>(
      ui::ImageModel::FromVectorIcon(icon, color, size));
}

std::unique_ptr<views::View> MakeKeycap(const std::u16string& key) {
  auto cap = MakeTile();
  cap->SetLayoutManager(std::make_unique<views::FillLayout>());
  cap->SetPreferredSize(gfx::Size(40, 40));
  auto label = MakeLabel(key, TextStyle::kBodyStrong);
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  cap->AddChildView(std::move(label));
  return cap;
}

std::u16string CountOf(size_t count,
                       std::u16string_view one,
                       std::u16string_view many) {
  return base::NumberToString16(count) + u" " +
         std::u16string(count == 1 ? one : many);
}

AccentCheckbox::AccentCheckbox() {
  // The tile around it is the target; the tick needs no padding of its own.
  SetBorder(views::CreateEmptyBorder(gfx::Insets()));
}

AccentCheckbox::~AccentCheckbox() = default;

void AccentCheckbox::OnThemeChanged() {
  views::Checkbox::OnThemeChanged();
  SetCheckedIconImageColor(
      GetColorProvider()->GetColor(kColorArciumWelcomeAccent));
}

BEGIN_METADATA(AccentCheckbox)
END_METADATA

AccentRadioButton::AccentRadioButton(const std::u16string& label, int group)
    : views::RadioButton(label, group) {}

AccentRadioButton::~AccentRadioButton() = default;

void AccentRadioButton::OnThemeChanged() {
  views::RadioButton::OnThemeChanged();
  SetCheckedIconImageColor(
      GetColorProvider()->GetColor(kColorArciumWelcomeAccent));
}

BEGIN_METADATA(AccentRadioButton)
END_METADATA

std::unique_ptr<views::View> MakeSpaceRow(const std::u16string& icon,
                                          const std::u16string& name,
                                          const std::u16string& detail) {
  auto row = std::make_unique<views::BoxLayoutView>();
  row->SetBetweenChildSpacing(14);
  row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  row->AddChildView(MakeBadge(icon, name, 34));
  auto* words = row->AddChildView(std::make_unique<views::BoxLayoutView>());
  words->SetOrientation(views::BoxLayout::Orientation::kVertical);
  words->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
  words->SetBetweenChildSpacing(2);
  words->AddChildView(MakeLabel(name, TextStyle::kBodyStrong));
  if (!detail.empty()) {
    words->AddChildView(MakeLabel(detail, TextStyle::kCaption));
  }
  row->SetFlexForView(words, 1);
  return row;
}

std::unique_ptr<views::View> MakeRule() {
  auto rule = std::make_unique<views::View>();
  rule->SetBackground(
      views::CreateSolidBackground(kColorArciumWelcomeTileBorder));
  rule->SetPreferredSize(gfx::Size(1, 1));
  return rule;
}

}  // namespace arcium::welcome
