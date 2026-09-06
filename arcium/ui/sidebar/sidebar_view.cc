// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/sidebar_view.h"

#include <memory>
#include <optional>
#include <utility>

#include "arcium/ui/sidebar/favorites_grid_view.h"
#include "arcium/ui/sidebar/nav_row_view.h"
#include "arcium/ui/sidebar/section_divider_view.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/space_bar_view.h"
#include "arcium/ui/sidebar/tab_list_view.h"
#include "arcium/ui/sidebar/tint_background.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

SidebarView::SidebarView(SidebarModel* model, Delegate delegate)
    : model_(model), delegate_(std::move(delegate)) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kVertical)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch)
      .SetInteriorMargin(gfx::Insets(metrics::kSidebarPadding))
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(3, 0));
  SetBackground(std::make_unique<TintBackground>());

  NavRowView::Delegate nav;
  nav.toggle_sidebar = delegate_.toggle_sidebar;
  nav.back = delegate_.back;
  nav.forward = delegate_.forward;
  nav.reload = delegate_.reload;
  nav_row_ = AddChildView(std::make_unique<NavRowView>(std::move(nav)));
  url_pill_ = AddChildView(std::make_unique<UrlPillView>(delegate_.edit_url));
  favorites_ = AddChildView(std::make_unique<FavoritesGridView>(model_));
  pinned_ = AddChildView(
      std::make_unique<TabListView>(model_, SidebarSection::kPinned));
  divider_ = AddChildView(std::make_unique<SectionDividerView>(
      base::BindRepeating(&SidebarModel::ClearToday, base::Unretained(model_))));
  // Today scrolls: with enough tabs the rows would otherwise be laid out past
  // the bottom of the column at zero height, which hides them entirely.
  // ScrollWithLayers is the macOS default, but a layer-backed viewport is not
  // opaque over the sidebar gradient (views::Label DCHECKs) and hides the rows
  // from the offscreen paint that --snapshot uses.
  today_scroll_ = AddChildView(std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kDisabled));
  today_ = today_scroll_->SetContents(
      std::make_unique<TabListView>(model_, SidebarSection::kToday));
  // Without a height clamp the viewport never sizes its contents (ScrollView
  // only does that for a bounded scroll view or a layer-backed one), so the
  // rows stay at zero. The upper bound is the whole column; FlexLayout gives
  // the scroll view whatever height is left after the sections above it.
  today_scroll_->ClipHeightTo(0, 100000);
  today_scroll_->SetBackgroundColor(std::nullopt);
  today_scroll_->SetDrawOverflowIndicator(false);
  today_scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  today_scroll_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  // Absorbs the leftover height, so the space bar stays at the bottom.
  today_scroll_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kVertical,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  space_bar_ = AddChildView(std::make_unique<SpaceBarView>());

  observation_.Observe(model_);
  Rebuild();
}

SidebarView::~SidebarView() = default;

void SidebarView::SetCaptionButtonWidth(int width) {
  caption_button_width_ = width;
  nav_row_->SetLeadingInset(width);
}

bool SidebarView::IsPositionInWindowCaption(const gfx::Point& point) const {
  if (nav_row_->bounds().Contains(point)) {
    gfx::Point p = point;
    ConvertPointToTarget(this, nav_row_, &p);
    return nav_row_->IsPointOnBackground(p);
  }
  // Empty space below the last row and above the space bar drags the window.
  const int last_row_bottom =
      today_scroll_->y() + today_->GetPreferredSize().height();
  return point.y() > last_row_bottom && point.y() < space_bar_->y();
}

void SidebarView::OnSidebarModelChanged() {
  Rebuild();
}

gfx::Size SidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(metrics::kSidebarWidth,
                   available_size.height().value_or(0));
}

void SidebarView::Rebuild() {
  // Rebuilt wholesale on every change: cheap at tens of rows, and the model
  // coalesces bursts. Each section reuses its row views by position.
  const std::vector<SidebarRow> rows = model_->rows();
  favorites_->SetRows(rows);
  pinned_->SetRows(rows);
  today_->SetRows(rows);
  if (!url_pill_->has_hosted_view()) {
    for (const SidebarRow& row : rows) {
      if (row.is_active) {
        url_pill_->SetPlaceholderText(base::UTF8ToUTF16(row.url.host()));
        break;
      }
    }
  }
  InvalidateLayout();
}

BEGIN_METADATA(SidebarView)
END_METADATA

}  // namespace arcium
