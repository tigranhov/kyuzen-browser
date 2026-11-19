// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/command_box_row.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

namespace {
constexpr int kRowHeight = 36;
constexpr float kRowCornerRadius = 8;
// The sidebar's columns: 8 in, a 16pt icon, 12 to the text.
constexpr int kRowInset = 8;
constexpr int kIconSize = 16;
constexpr int kIconTextGap = 12;
constexpr int kKeycapRadius = 4;

// The glyph a row draws before, or instead of, a site's icon: a search for a
// search, a keyboard for a command, a globe for a page.
const gfx::VectorIcon& KindIcon(const SuggestionRow& row) {
  if (row.command_id) {
    return vector_icons::kKeyboardIcon;
  }
  if (row.is_search) {
    return vector_icons::kSearchIcon;
  }
  return vector_icons::kGlobeIcon;
}

std::unique_ptr<views::Label> MakeAside(const std::u16string& text) {
  auto label = std::make_unique<views::Label>(text);
  label->SetEnabledColor(kColorArciumRowTextSecondary);
  label->SetFontList(
      views::Label::GetDefaultFontList().DeriveWithSizeDelta(-1));
  label->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 8, 0, 0));
  return label;
}

}  // namespace

CommandBoxRow::CommandBoxRow(const SuggestionRow& row,
                             const std::u16string& shortcut,
                             base::RepeatingClosure on_chosen)
    : on_chosen_(std::move(on_chosen)) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::VH(0, kRowInset));
  SetPreferredSize(gfx::Size(0, kRowHeight));

  auto icon = std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
      KindIcon(row), kColorArciumRowTextSecondary, kIconSize));
  icon->SetImageSize(gfx::Size(kIconSize, kIconSize));
  icon->SetProperty(views::kMarginsKey,
                    gfx::Insets::TLBR(0, 0, 0, kIconTextGap));
  icon_ = AddChildView(std::move(icon));

  auto title = std::make_unique<views::Label>(row.title);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetElideBehavior(gfx::ELIDE_TAIL);
  title->SetEnabledColor(kColorArciumRowTextActive);
  title->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kPreferred)
          .WithOrder(2));
  AddChildView(std::move(title));

  if (!row.subtitle.empty()) {
    auto subtitle = MakeAside(row.subtitle);
    subtitle->SetElideBehavior(gfx::ELIDE_HEAD);
    subtitle->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kPreferred)
            .WithOrder(1));
    AddChildView(std::move(subtitle));
  }

  // Everything after this sits at the right edge.
  auto spacer = std::make_unique<views::View>();
  spacer->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded)
          .WithOrder(3));
  AddChildView(std::move(spacer));

  if (row.is_open_tab) {
    // What Enter does, said plainly: it goes to the tab rather than loading
    // the page a second time. "Open tab" read as a command.
    aside_ = AddChildView(MakeAside(u"Switch to tab"));
  } else if (!shortcut.empty()) {
    auto keycap = MakeAside(shortcut);
    keycap->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(1, 6)));
    keycap->SetBackground(views::CreateRoundedRectBackground(
        kColorArciumControlHoverBackground, kKeycapRadius));
    aside_ = AddChildView(std::move(keycap));
  }

  enter_mark_ = AddChildView(MakeAside(u"\u21B5"));
  enter_mark_->SetVisible(false);
}

CommandBoxRow::~CommandBoxRow() = default;

void CommandBoxRow::SetSelected(bool selected) {
  if (selected == selected_) {
    return;
  }
  selected_ = selected;
  enter_mark_->SetVisible(selected_);
  UpdateBackground();
}

void CommandBoxRow::SetIcon(const ui::ImageModel& icon) {
  if (!icon.IsEmpty()) {
    icon_->SetImage(icon);
  }
}

void CommandBoxRow::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateBackground();
}

bool CommandBoxRow::OnMousePressed(const ui::MouseEvent& event) {
  // Taken, so that the release comes here too: a row that refuses the press
  // never hears where the click ended.
  return event.IsOnlyLeftMouseButton();
}

void CommandBoxRow::OnMouseReleased(const ui::MouseEvent& event) {
  // Only a release inside the row counts, so a press that the reader drags
  // away from is a press they changed their mind about.
  if (event.IsOnlyLeftMouseButton() && HitTestPoint(event.location()) &&
      on_chosen_) {
    on_chosen_.Run();
  }
}

void CommandBoxRow::OnMouseEntered(const ui::MouseEvent&) {
  hovered_ = true;
  UpdateBackground();
}

void CommandBoxRow::OnMouseExited(const ui::MouseEvent&) {
  hovered_ = false;
  UpdateBackground();
}

void CommandBoxRow::UpdateBackground() {
  // Two marks, because the pointer and the keyboard can be on different rows
  // at once: the row Enter would take is the stronger one, and the row under
  // the pointer is a lighter wash that goes away when the pointer does.
  if (selected_) {
    SetBackground(views::CreateRoundedRectBackground(
        kColorArciumRowActiveBackground, kRowCornerRadius));
  } else if (hovered_) {
    SetBackground(views::CreateRoundedRectBackground(
        kColorArciumRowHoverBackground, kRowCornerRadius));
  } else {
    SetBackground(nullptr);
  }
}

BEGIN_METADATA(CommandBoxRow)
END_METADATA

}  // namespace arcium
