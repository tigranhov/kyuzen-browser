// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/extensions_row_view.h"

#include <algorithm>
#include <utility>

#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

ExtensionsRowView::ExtensionsRowView() = default;

ExtensionsRowView::~ExtensionsRowView() = default;

views::View* ExtensionsRowView::SetHostedView(
    std::unique_ptr<views::View> view) {
  hosted_ = AddChildView(std::move(view));
  hosted_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  observation_.Observe(hosted_.get());
  PreferredSizeChanged();
  return hosted_;
}

void ExtensionsRowView::Layout(PassKey) {
  if (!hosted_) {
    return;
  }
  hosted_->SetBoundsRect(GetLocalBounds());
  const int per_line = ButtonsPerLine(width());
  const int step = metrics::kExtensionButtonSize + metrics::kExtensionButtonGap;
  int index = 0;
  for (views::View* button : hosted_->children()) {
    if (!button->GetVisible()) {
      continue;
    }
    button->SetBounds((index % per_line) * step, (index / per_line) * step,
                      metrics::kExtensionButtonSize,
                      metrics::kExtensionButtonSize);
    ++index;
  }
}

gfx::Size ExtensionsRowView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int count = VisibleButtonCount();
  if (count == 0) {
    // An empty row is not a thin gap above the favourites: it is nothing.
    return gfx::Size(0, 0);
  }
  const int width = available_size.width().value_or(metrics::kSidebarWidth);
  const int per_line = ButtonsPerLine(width);
  const int lines = (count + per_line - 1) / per_line;
  return gfx::Size(width, lines * metrics::kExtensionButtonSize +
                              (lines - 1) * metrics::kExtensionButtonGap);
}

void ExtensionsRowView::OnChildViewAdded(views::View* observed,
                                         views::View* child) {
  PreferredSizeChanged();
}

void ExtensionsRowView::OnChildViewRemoved(views::View* observed,
                                           views::View* child) {
  PreferredSizeChanged();
}

void ExtensionsRowView::OnViewVisibilityChanged(views::View* observed,
                                                views::View* starting_view,
                                                bool visible) {
  PreferredSizeChanged();
}

int ExtensionsRowView::ButtonsPerLine(int width) const {
  return std::max(
      1, (width + metrics::kExtensionButtonGap) /
             (metrics::kExtensionButtonSize + metrics::kExtensionButtonGap));
}

int ExtensionsRowView::VisibleButtonCount() const {
  if (!hosted_) {
    return 0;
  }
  return static_cast<int>(std::count_if(
      hosted_->children().begin(), hosted_->children().end(),
      [](const views::View* child) { return child->GetVisible(); }));
}

BEGIN_METADATA(ExtensionsRowView)
END_METADATA

}  // namespace arcium
