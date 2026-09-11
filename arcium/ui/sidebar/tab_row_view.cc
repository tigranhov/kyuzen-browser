// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tab_row_view.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/row_drag_image.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/unloaded_row_dimming.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
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
  // A row is its own drag source. What it writes is an id, never itself: see
  // RowDragData.
  set_drag_controller(this);
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
  // The hover buttons appear under the cursor that summoned them. Views'
  // default counts a view as entered only while the mouse is over it and NOT
  // over a descendant, so without this the button hides the instant it
  // appears, uncovering the row, which shows it again -- a flicker for as
  // long as the pointer rests there, and the row's hover background blinks
  // with it.
  SetNotifyEnterExitOnChild(true);
  close_->SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  close_->GetViewAccessibility().SetName(u"Close tab");
}

TabRowView::~TabRowView() = default;

void TabRowView::SetRow(const SidebarRow& row) {
  // Row views are pooled by laid-out position, so this slot can be handed a
  // different entry at any time — another window on the same profile unpins
  // something and every list rebuilds. An open field belongs to the entry it
  // was opened on, not to the slot, so it goes rather than hovers over a row
  // that is not its own.
  if (is_renaming() && row.entry_id != renaming_entry_id_) {
    AbandonRename();
  }
  row_ = row;
  UpdateVisuals();
}

void TabRowView::UpdateVisuals() {
  favicon_->SetImage(row_.needs_load() ? DimUnloadedFavicon(row_.favicon)
                                       : row_.favicon);
  favicon_->SetVisible(!row_.is_loading);
  throbber_->SetVisible(row_.is_loading);
  if (row_.is_loading) {
    throbber_->Start();
  } else {
    throbber_->Stop();
  }
  title_->SetText(row_.title);
  // An active row is never cold and never unloaded -- but the active check
  // still goes first, so that invariant is never load-bearing here.
  title_->SetEnabledColor(row_.is_active      ? kColorArciumRowTextActive
                          : row_.needs_load() ? kColorArciumRowTextUnloaded
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
  // A cold entry has no tab and a Today tab has no entry, but both can be
  // named; a row that is neither draws nothing this could rename.
  const bool nameable = row_.entry_id.is_valid() || row_.tab_index >= 0;
  if (!nameable || is_renaming()) {
    return;
  }
  renaming_entry_id_ = row_.entry_id;
  renaming_row_ = row_;
  auto field = std::make_unique<RenameField>(
      row_.title, base::BindOnce(&TabRowView::OnRenameFinished,
                                 weak_factory_.GetWeakPtr(), renaming_row_));
  field->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  // In the title's place, so the favicon stays to its left.
  rename_field_ = AddChildViewAt(std::move(field), GetIndexOf(title_).value());
  // The field takes the focus, and a focusable view without a name is a view
  // a screen reader cannot announce.
  rename_field_->GetViewAccessibility().SetName(u"Tab name");
  UpdateTrailingButtons();
  rename_field_->RequestFocus();
  InvalidateLayout();
}

void TabRowView::AbandonRename() {
  if (!rename_field_) {
    return;
  }
  RenameField* field = rename_field_.ExtractAsDangling();
  field->Abandon();
  renaming_entry_id_ = EntryId();
  RemoveChildViewT(field);
  UpdateTrailingButtons();
  InvalidateLayout();
}

void TabRowView::OnRenameFinished(const SidebarRow& row,
                                  bool commit,
                                  const std::u16string& title) {
  if (rename_field_) {
    RemoveChildViewT(rename_field_.ExtractAsDangling());
    renaming_entry_id_ = EntryId();
  }
  UpdateTrailingButtons();
  InvalidateLayout();
  // `row` is the row the edit was started on, not whatever this pooled view
  // draws now: the model can move — from another window on the same profile —
  // between the Enter and this posted task, and the write must land on what
  // the user was typing into. A model that no longer has it makes the command
  // a no-op, which is the right answer.
  //
  // An empty title is a command here rather than nothing: on a Today tab it
  // clears the custom name and the row follows the page again. On an entry it
  // is still refused, because a permanent title has no page to fall back to.
  const bool clearing = title.empty();
  if (!commit || !delegate_.rename || (clearing && row.entry_id.is_valid())) {
    return;
  }
  base::RepeatingCallback<void(const SidebarRow&, const std::u16string&)>
      rename = delegate_.rename;
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

bool TabRowView::BeginRenameFromDoubleClick() {
  // Click 1 activated this row, which is why the gesture is safe here and was
  // not on a folder header: activating the row you are about to rename is the
  // row you meant, while click 1 on a header had already collapsed it.
  BeginRename();
  return is_renaming();
}

bool TabRowView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyMiddleMouseButton()) {
    // Copies: the close destroys this view before Run() returns.
    base::RepeatingCallback<void(const SidebarRow&)> close = delegate_.close;
    const SidebarRow row = row_;
    close.Run(row);
    return true;
  }
  rename_began_on_press_ = false;
  // R2.4: a double-click renames the row. This is the second press of the
  // pair, and it is also the press a drag would start from, so the two are
  // settled here rather than by whichever handler wins later:
  // CanStartDragForView refuses while a rename is open, and the button is not
  // told about this press at all, so the release cannot activate the row a
  // second time. A row with no entry has nothing to carry a name past its
  // tab's life, and BeginRename refuses it — the press then falls through and
  // behaves like any other click.
  if (event.IsOnlyLeftMouseButton() &&
      (event.flags() & ui::EF_IS_DOUBLE_CLICK) && !is_renaming() &&
      BeginRenameFromDoubleClick()) {
    rename_began_on_press_ = true;
    return true;
  }
  return views::Button::OnMousePressed(event);
}

void TabRowView::OnMouseReleased(const ui::MouseEvent& event) {
  if (rename_began_on_press_) {
    // The press opened the field; the release must not also fire the button,
    // which would activate the row underneath the edit.
    rename_began_on_press_ = false;
    // View::ProcessMousePressed computed the drag operations, and so set
    // `possible_drag`, before OnMousePressed opened the field. So a hand that
    // moved during the double-click still reached ProcessMouseDragged;
    // CanStartDragForView refused it and the else branch handed the move to
    // Button::OnMouseDragged, which paints the row pressed. This release is
    // the last thing that will ever touch it, so it is where the paint has to
    // come back — under an open rename field is where it would show most.
    // Where ButtonController::OnMouseReleased would have put it, which is the
    // whole point: the release puts the button back without firing it.
    SetState(HitTestPoint(event.location()) ? STATE_HOVERED : STATE_NORMAL);
    return;
  }
  views::Button::OnMouseReleased(event);
}

void TabRowView::WriteDragDataForView(views::View* sender,
                                      const gfx::Point& press_pt,
                                      ui::OSExchangeData* data) {
  RowDragData payload;
  payload.entry_id = row_.entry_id;
  payload.tab_index = row_.tab_index;
  payload.Write(data);
  SetRowDragImage(row_, sender, press_pt, data);
  // Once per drag, at the one moment a source knows one is starting.
  if (delegate_.drag_started) {
    delegate_.drag_started.Run();
  }
}

int TabRowView::GetDragOperationsForView(views::View* sender,
                                         const gfx::Point& p) {
  // A row that names neither an entry nor a tab is not a row anything can be
  // told to move, and while a rename is open the field owns the row.
  if (is_renaming() || (!row_.entry_id.is_valid() && row_.tab_index < 0)) {
    return ui::DragDropTypes::DRAG_NONE;
  }
  // Never DRAG_COPY: a sidebar row is one thing in one place, and two rows
  // for one entry is a state the model cannot hold.
  return ui::DragDropTypes::DRAG_MOVE;
}

bool TabRowView::CanStartDragForView(views::View* sender,
                                     const gfx::Point& press_pt,
                                     const gfx::Point& p) {
  if (rename_began_on_press_ || is_renaming()) {
    return false;
  }
  // Views' own threshold, the same one ProcessMouseDragged already applied,
  // so a row starts dragging exactly when every other draggable view does.
  return views::View::ExceededDragThreshold(press_pt - p);
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
