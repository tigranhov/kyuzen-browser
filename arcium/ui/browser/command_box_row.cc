// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/command_box_row.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
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

CommandBoxRow::CommandBoxRow(const SuggestionRow& row) {
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
  OnThemeChanged();
}

void CommandBoxRow::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(selected_
                    ? views::CreateRoundedRectBackground(
                          kColorArciumRowHoverBackground, kRowCornerRadius)
                    : nullptr);
}

BEGIN_METADATA(CommandBoxRow)
END_METADATA

}  // namespace arcium
