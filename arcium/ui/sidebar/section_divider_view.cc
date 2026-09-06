// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/section_divider_view.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

namespace {
constexpr int kDividerHeight = 20;
}  // namespace

SectionDividerView::SectionDividerView(base::RepeatingClosure on_clear) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::VH(0, metrics::kRowHorizontalPadding));
  line_ = AddChildView(std::make_unique<views::Separator>());
  line_->SetOrientation(views::Separator::Orientation::kHorizontal);
  line_->SetColorId(kColorArciumDivider);
  line_->SetProperty(views::kCrossAxisAlignmentKey,
                     views::LayoutAlignment::kCenter);
  line_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  clear_ = AddChildView(
      std::make_unique<views::LabelButton>(std::move(on_clear), u"Clear"));
  clear_->SetEnabledTextColors(kColorArciumRowTextSecondary);
  clear_->SetVisible(false);
  clear_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 8, 0, 0));
}

SectionDividerView::~SectionDividerView() = default;

void SectionDividerView::OnMouseEntered(const ui::MouseEvent& event) {
  clear_->SetVisible(true);
}

void SectionDividerView::OnMouseExited(const ui::MouseEvent& event) {
  // Keep it while the pointer is over the button itself.
  if (!clear_->IsMouseHovered()) {
    clear_->SetVisible(false);
  }
}

gfx::Size SectionDividerView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(metrics::kSidebarWidth),
                   kDividerHeight);
}

BEGIN_METADATA(SectionDividerView)
END_METADATA

}  // namespace arcium
