// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/sidebar_view.h"

#include <memory>

#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

SidebarView::SidebarView(SidebarModel* model) : model_(model) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kVertical)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch)
      .SetInteriorMargin(gfx::Insets(metrics::kSidebarPadding))
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(3, 0));
  // Placeholder until Task 8 installs the tinted background.
  SetBackground(views::CreateSolidBackground(SkColorSetRGB(0x1C, 0x1C, 0x22)));
  // Sections are attached by Rebuild(); tasks 6 to 9 fill them in.
  observation_.Observe(model_);
  Rebuild();
}

SidebarView::~SidebarView() = default;

void SidebarView::SetCaptionButtonWidth(int width) {
  // Consumed by NavRowView from Task 7 on.
  caption_button_width_ = width;
}

bool SidebarView::IsPositionInWindowCaption(const gfx::Point& point) const {
  // Task 7 refines this once NavRowView exists: only the nav row background
  // and empty space are draggable. Until then, the top strip is.
  return point.y() < metrics::kSidebarPadding + metrics::kNavButtonSize;
}

void SidebarView::OnSidebarModelChanged() {
  Rebuild();
}

gfx::Size SidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(metrics::kSidebarWidth,
                   available_size.height().value_or(0));
}

void SidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // Task 8 installs the tinted background here.
}

void SidebarView::Rebuild() {
  // Rebuilt wholesale on every change: cheap at tens of rows, and the model
  // coalesces bursts. Task 6 adds the section views and hands them rows().
}

BEGIN_METADATA(SidebarView)
END_METADATA

}  // namespace arcium
