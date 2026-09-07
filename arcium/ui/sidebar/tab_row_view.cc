// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tab_row_view.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

namespace {
constexpr int kDragThreshold = 4;
constexpr int kIndicatorSize = 14;
}  // namespace

TabRowView::TabRowView(Delegate delegate)
    : views::Button(base::BindRepeating(
          [](TabRowView* self) {
            // Activating rebuilds the list, which can destroy this view, so
            // both the callback and the row it is handed are copies off the
            // dying object rather than references into it.
            base::RepeatingCallback<void(const SidebarRow&)> activate =
                self->delegate_.activate;
            const SidebarRow row = self->row_;
            activate.Run(row);
          },
          base::Unretained(this))),
      delegate_(std::move(delegate)) {
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::VH(0, metrics::kRowHorizontalPadding))
      .SetDefault(views::kMarginsKey,
                  gfx::Insets::VH(0, metrics::kRowIconTextGap / 2));

  favicon_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_->SetImageSize(
      gfx::Size(metrics::kFaviconSize, metrics::kFaviconSize));
  throbber_ = AddChildView(std::make_unique<views::Throbber>());
  throbber_->SetPreferredSize(
      gfx::Size(metrics::kFaviconSize, metrics::kFaviconSize));
  throbber_->SetVisible(false);

  title_ = AddChildView(std::make_unique<views::Label>());
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  // Rows live inside a ScrollView, which is layer-backed on macOS, and the
  // sidebar's gradient means that layer is not opaque.
  title_->SetSubpixelRenderingEnabled(false);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  audio_ = AddChildView(std::make_unique<views::ImageView>());
  audio_->SetVisible(false);

  close_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(
          [](TabRowView* self) {
            // Closing destroys this view; see the activate callback above.
            base::RepeatingCallback<void(const SidebarRow&)> close =
                self->delegate_.close;
            const SidebarRow row = self->row_;
            close.Run(row);
          },
          base::Unretained(this)),
      kCloseIcon, kIndicatorSize));
  close_->SetVisible(false);
  close_->SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  close_->GetViewAccessibility().SetName(u"Close tab");
}

TabRowView::~TabRowView() = default;

void TabRowView::SetRow(const SidebarRow& row) {
  row_ = row;
  UpdateVisuals();
}

void TabRowView::UpdateVisuals() {
  favicon_->SetImage(row_.favicon);
  favicon_->SetVisible(!row_.is_loading);
  throbber_->SetVisible(row_.is_loading);
  if (row_.is_loading) {
    throbber_->Start();
  } else {
    throbber_->Stop();
  }
  title_->SetText(row_.title);
  title_->SetEnabledColor(row_.is_active ? kColorArciumRowTextActive
                                         : kColorArciumRowText);
  if (row_.is_audible || row_.is_muted) {
    audio_->SetImage(
        ui::ImageModel::FromVectorIcon(row_.is_muted ? kMutedIcon : kAudioIcon,
                                       kColorArciumRowText, kIndicatorSize));
  }
  GetViewAccessibility().SetName(row_.title);
  UpdateCloseButtonVisibility();
  OnThemeChanged();
}

void TabRowView::UpdateCloseButtonVisibility() {
  close_->SetVisible(hovered_);
  // The audio indicator yields its slot to the close button on hover.
  audio_->SetVisible(!hovered_ && (row_.is_audible || row_.is_muted));
}

bool TabRowView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyMiddleMouseButton()) {
    // Copies: the close destroys this view before Run() returns.
    base::RepeatingCallback<void(const SidebarRow&)> close = delegate_.close;
    const SidebarRow row = row_;
    close.Run(row);
    return true;
  }
  drag_start_ = event.location();
  dragging_ = false;
  return views::Button::OnMousePressed(event);
}

bool TabRowView::OnMouseDragged(const ui::MouseEvent& event) {
  if (!dragging_ &&
      std::abs(event.location().y() - drag_start_.y()) > kDragThreshold) {
    dragging_ = true;
  }
  if (dragging_) {
    // Ask the list to move us when the pointer crosses a neighbour's midline.
    const int rows_moved = (event.location().y() - drag_start_.y()) / height();
    if (rows_moved != 0) {
      delegate_.drag_move.Run(tab_index(), tab_index() + rows_moved);
    }
    return true;
  }
  return views::Button::OnMouseDragged(event);
}

void TabRowView::OnMouseReleased(const ui::MouseEvent& event) {
  const bool was_dragging = dragging_;
  dragging_ = false;
  if (!was_dragging) {
    views::Button::OnMouseReleased(event);
  }
}

void TabRowView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  UpdateCloseButtonVisibility();
  OnThemeChanged();
}

void TabRowView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  UpdateCloseButtonVisibility();
  OnThemeChanged();
}

void TabRowView::OnThemeChanged() {
  views::Button::OnThemeChanged();
  if (row_.is_active) {
    SetBackground(views::CreateRoundedRectBackground(
        kColorArciumRowActiveBackground, metrics::kRowCornerRadius));
  } else if (hovered_) {
    SetBackground(views::CreateRoundedRectBackground(
        kColorArciumRowHoverBackground, metrics::kRowCornerRadius));
  } else {
    SetBackground(nullptr);
  }
}

gfx::Size TabRowView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(metrics::kSidebarWidth),
                   metrics::kRowHeight);
}

BEGIN_METADATA(TabRowView)
END_METADATA

}  // namespace arcium
