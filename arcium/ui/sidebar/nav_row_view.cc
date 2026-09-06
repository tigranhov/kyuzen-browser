// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/nav_row_view.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

namespace {
constexpr int kNavIconSize = 16;
constexpr float kNavButtonRadius = 6;
}  // namespace

NavRowView::NavRowView(Delegate delegate) : delegate_(std::move(delegate)) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(0, 2));

  leading_spacer_ = AddChildView(std::make_unique<views::View>());
  toggle_ = AddButton(delegate_.toggle_sidebar, kSidebarIcon, u"Hide sidebar");

  flex_spacer_ = AddChildView(std::make_unique<views::View>());
  flex_spacer_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  back_ = AddButton(delegate_.back, kBackIcon, u"Back");
  forward_ = AddButton(delegate_.forward, kForwardIcon, u"Forward");
  reload_ = AddButton(delegate_.reload, kReloadIcon, u"Reload");
  SetLeadingInset(0);
}

NavRowView::~NavRowView() = default;

views::ImageButton* NavRowView::AddButton(base::RepeatingClosure callback,
                                          const gfx::VectorIcon& icon,
                                          const std::u16string& tooltip) {
  auto button = views::CreateVectorImageButtonWithNativeTheme(
      std::move(callback), icon, kNavIconSize);
  button->SetTooltipText(tooltip);
  button->SetPreferredSize(
      gfx::Size(metrics::kNavButtonSize, metrics::kNavButtonSize));
  views::InstallRoundRectHighlightPathGenerator(button.get(), gfx::Insets(),
                                                kNavButtonRadius);
  return AddChildView(std::move(button));
}

void NavRowView::SetLeadingInset(int inset) {
  leading_spacer_->SetPreferredSize(gfx::Size(inset, metrics::kNavButtonSize));
  InvalidateLayout();
}

void NavRowView::SetBackEnabled(bool enabled) {
  back_->SetEnabled(enabled);
}

void NavRowView::SetForwardEnabled(bool enabled) {
  forward_->SetEnabled(enabled);
}

bool NavRowView::IsPointOnBackground(const gfx::Point& point) const {
  for (const views::View* child : children()) {
    if (child != leading_spacer_ && child != flex_spacer_ &&
        child->bounds().Contains(point)) {
      return false;
    }
  }
  return true;
}

BEGIN_METADATA(NavRowView)
END_METADATA

}  // namespace arcium
