// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tab_row_view.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
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
  set_context_menu_controller(this);
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

  revert_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(&TabRowView::Revert, base::Unretained(this)),
      kRevertIcon, kIndicatorSize));
  revert_->SetVisible(false);
  revert_->SetID(kRevertButtonId);
  revert_->SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  revert_->SetTooltipText(u"Return to pinned URL");
  revert_->GetViewAccessibility().SetName(u"Return to pinned URL");

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
  close_->SetID(kCloseButtonId);
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
  UpdateTrailingButtons();
  OnThemeChanged();
}

void TabRowView::UpdateTrailingButtons() {
  // The field owns the whole row while it is up: nothing else is actionable
  // and a stray close would drop the edit on the floor.
  const bool renaming = is_renaming();
  title_->SetVisible(!renaming);
  // A pinned entry that has navigated away offers the way back in the slot
  // the close button would otherwise take. Closing such a row is still
  // reachable by middle click and from the context menu.
  const bool show_revert =
      hovered_ && !renaming && row_.can_return_to_pinned_url;
  revert_->SetVisible(show_revert);
  close_->SetVisible(hovered_ && !renaming && !show_revert);
  // The audio indicator yields its slot to the hover buttons.
  audio_->SetVisible(!hovered_ && !renaming &&
                     (row_.is_audible || row_.is_muted));
}

void TabRowView::BeginRename() {
  // A Today tab has no entry, so there is nothing to carry the name past the
  // tab's life; renaming it would be a lie.
  if (!row_.entry_id.is_valid() || is_renaming()) {
    return;
  }
  auto field = std::make_unique<RenameField>(
      row_.title, base::BindOnce(&TabRowView::OnRenameFinished,
                                 weak_factory_.GetWeakPtr()));
  field->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  // In the title's place, so the favicon stays to its left.
  rename_field_ = AddChildViewAt(std::move(field), GetIndexOf(title_).value());
  UpdateTrailingButtons();
  rename_field_->RequestFocus();
  InvalidateLayout();
}

void TabRowView::OnRenameFinished(bool commit, const std::u16string& title) {
  if (rename_field_) {
    RemoveChildViewT(rename_field_.ExtractAsDangling());
  }
  UpdateTrailingButtons();
  InvalidateLayout();
  if (!commit || title.empty() || !row_.entry_id.is_valid() ||
      !delegate_.rename) {
    return;
  }
  // Copies: the rename rebuilds the list and can destroy this view.
  base::RepeatingCallback<void(const SidebarRow&, const std::u16string&)>
      rename = delegate_.rename;
  const SidebarRow row = row_;
  rename.Run(row, title);
}

void TabRowView::Revert() {
  if (!delegate_.return_to_pinned_url || !row_.can_return_to_pinned_url) {
    return;
  }
  base::RepeatingCallback<void(const SidebarRow&)> revert =
      delegate_.return_to_pinned_url;
  const SidebarRow row = row_;
  revert.Run(row);
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
  UpdateTrailingButtons();
  OnThemeChanged();
}

void TabRowView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  UpdateTrailingButtons();
  OnThemeChanged();
}

bool TabRowView::OnKeyPressed(const ui::KeyEvent& event) {
  // Cmd+Shift+Backspace on a row that has the focus. SidebarView carries the
  // same binding as an accelerator for the active row, which is the reachable
  // path while rows are only accessibility-focusable.
  if (event.key_code() == ui::VKEY_BACK && event.IsShiftDown() &&
      (event.IsCommandDown() || event.IsControlDown())) {
    if (row_.can_return_to_pinned_url) {
      Revert();
      return true;
    }
  }
  return views::Button::OnKeyPressed(event);
}

void TabRowView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!delegate_.show_context_menu || is_renaming()) {
    return;
  }
  delegate_.show_context_menu.Run(this, row_, point);
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
