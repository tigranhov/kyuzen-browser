// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/extensions_row_view.h"

#include <algorithm>
#include <utility>

#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

ExtensionsRowView::ExtensionsRowView() {
  // The row's edge has to cut off what the strip draws beyond it, and a view's
  // bounds clip only what paints into its parent. The strip paints to a layer
  // of its own and so does any of its buttons while it is lit, which escaped:
  // the menu button tucked above the row, lit while the menu hanging from it
  // was open, sat over the pill as a large puzzle piece. A layer that masks to
  // the row's bounds clips those too.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetMasksToBounds(true);
}

ExtensionsRowView::~ExtensionsRowView() = default;

views::View* ExtensionsRowView::SetHostedView(
    std::unique_ptr<views::View> view) {
  hosted_ = AddChildView(std::move(view));
  hosted_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  observation_.Observe(hosted_.get());
  PreferredSizeChanged();
  return hosted_;
}

void ExtensionsRowView::SetSkippedButton(views::View* button) {
  skipped_ = button;
  PreferredSizeChanged();
}

void ExtensionsRowView::Layout(PassKey) {
  if (!hosted_) {
    return;
  }
  // As much room as the strip asks for, even when the row itself has none.
  // A row with nothing in it is laid out as nothing at all -- no height and
  // no width either -- and the strip keeps its own layout, which hides every
  // button that does not fit the box it is in. In a box of no size that is
  // all of them, including the one just pinned, which is what the row
  // measures itself by: the row could then never grow back. So the width
  // comes from the sidebar when the row has none of its own. Nothing
  // escapes: a row of no height paints nothing, whatever size the strip
  // inside it thinks it has.
  const int room = width() > 0                         ? width()
                   : parent() && parent()->width() > 0 ? parent()->width()
                                                       : metrics::kSidebarWidth;
  const int wanted =
      hosted_->GetPreferredSize(views::SizeBounds(room, {})).height();
  hosted_->SetBoundsRect(gfx::Rect(
      0, 0, room, std::max({height(), wanted, metrics::kExtensionButtonSize})));
  const int per_line = ButtonsPerLine(room);
  const int step = metrics::kExtensionButtonSize + metrics::kExtensionButtonGap;
  int index = 0;
  for (views::View* button : hosted_->children()) {
    if (!button->GetVisible()) {
      continue;
    }
    if (button == skipped_) {
      // Above the row, where the row's own layer cuts it off. Left where the
      // strip can still call it visible, because arguing with the strip about
      // that ends with it hiding everything else too -- and at its full size,
      // because a button with no room makes the strip drop pinned ones.
      button->SetBounds(0, -metrics::kExtensionButtonSize,
                        metrics::kExtensionButtonSize,
                        metrics::kExtensionButtonSize);
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
  const int width = available_size.width().value_or(metrics::kSidebarWidth);
  if (count == 0) {
    // An empty row is not a thin gap above the favourites: it is nothing.
    // No height, but the full width still, because the strip inside decides
    // what it can show from the width it is given, and a strip nought pixels
    // wide shows nothing -- including the button that was just pinned, which
    // would leave the row empty for ever.
    return gfx::Size(width, 0);
  }
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
  return static_cast<int>(
      std::count_if(hosted_->children().begin(), hosted_->children().end(),
                    [this](const views::View* child) {
                      return child->GetVisible() && child != skipped_;
                    }));
}

BEGIN_METADATA(ExtensionsRowView)
END_METADATA

}  // namespace arcium
