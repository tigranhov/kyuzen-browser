// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/url_pill_view.h"

#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"

namespace arcium {

UrlPillView::UrlPillView(base::RepeatingClosure on_click)
    : on_click_(std::move(on_click)) {
  SetLayoutManager(std::make_unique<views::FillLayout>());
  placeholder_ = AddChildView(std::make_unique<views::Label>());
  placeholder_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  placeholder_->SetElideBehavior(gfx::ELIDE_TAIL);
  placeholder_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 10)));
  placeholder_->SetEnabledColor(kColorArciumRowTextSecondary);
}

UrlPillView::~UrlPillView() = default;

void UrlPillView::SetPlaceholderText(const std::u16string& text) {
  placeholder_->SetText(text);
}

views::View* UrlPillView::SetHostedView(std::unique_ptr<views::View> view) {
  placeholder_->SetVisible(false);
  hosted_ = AddChildView(std::move(view));
  InvalidateLayout();
  return hosted_;
}

bool UrlPillView::OnMousePressed(const ui::MouseEvent& event) {
  if (!hosted_ && event.IsOnlyLeftMouseButton()) {
    on_click_.Run();
    return true;
  }
  return views::View::OnMousePressed(event);
}

void UrlPillView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(views::CreateRoundedRectBackground(
      kColorArciumControlBackground,
      static_cast<float>(metrics::kUrlPillHeight) / 3));
}

gfx::Size UrlPillView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(metrics::kSidebarWidth),
                   metrics::kUrlPillHeight);
}

BEGIN_METADATA(UrlPillView)
END_METADATA

}  // namespace arcium
