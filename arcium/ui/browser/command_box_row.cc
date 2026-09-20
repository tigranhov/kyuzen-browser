// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/command_box_row.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

namespace {
constexpr int kRowHeight = 36;
constexpr float kRowCornerRadius = 8;
}  // namespace

CommandBoxRow::CommandBoxRow(const SuggestionRow& row,
                             base::RepeatingClosure on_chosen)
    : on_chosen_(std::move(on_chosen)) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::VH(0, 10))
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(0, 4));
  SetPreferredSize(gfx::Size(0, kRowHeight));

  auto title = std::make_unique<views::Label>(row.title);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetElideBehavior(gfx::ELIDE_TAIL);
  title->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  AddChildView(std::move(title));

  if (!row.subtitle.empty()) {
    auto subtitle = std::make_unique<views::Label>(row.subtitle);
    subtitle->SetEnabledColor(kColorArciumRowTextSecondary);
    subtitle->SetElideBehavior(gfx::ELIDE_HEAD);
    subtitle->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kPreferred));
    AddChildView(std::move(subtitle));
  }

  if (row.is_open_tab) {
    // Said plainly, because what this row does is different: it goes to a tab
    // instead of loading the page a second time.
    auto open = std::make_unique<views::Label>(u"Open tab");
    open->SetEnabledColor(kColorArciumRowTextSecondary);
    AddChildView(std::move(open));
  }
}

CommandBoxRow::~CommandBoxRow() = default;

void CommandBoxRow::SetSelected(bool selected) {
  if (selected == selected_) {
    return;
  }
  selected_ = selected;
  UpdateBackground();
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
