// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/folder_header_view.h"

#include <memory>
#include <optional>
#include <utility>

#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/row_drag_image.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

FolderHeaderView::FolderHeaderView(Delegate delegate)
    : views::Button(base::BindRepeating(&FolderHeaderView::Toggle,
                                        base::Unretained(this))),
      delegate_(std::move(delegate)) {
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  set_context_menu_controller(this);
  set_drag_controller(this);
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::VH(0, metrics::kRowHorizontalPadding))
      .SetDefault(views::kMarginsKey,
                  gfx::Insets::VH(0, metrics::kRowIconTextGap / 2));

  disclosure_ = AddChildView(std::make_unique<views::ImageView>());
  disclosure_->SetImageSize(
      gfx::Size(metrics::kFaviconSize, metrics::kFaviconSize));

  name_ = AddChildView(std::make_unique<views::Label>());
  name_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  // See TabRowView: the sidebar's gradient is not an opaque background.
  name_->SetSubpixelRenderingEnabled(false);
  name_->SetElideBehavior(gfx::ELIDE_TAIL);
  name_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  count_ = AddChildView(std::make_unique<views::Label>());
  count_->SetSubpixelRenderingEnabled(false);
  // As in TabRowView: the header's hover background must survive the cursor
  // crossing its own label, which is a descendant.
  SetNotifyEnterExitOnChild(true);
}

FolderHeaderView::~FolderHeaderView() = default;

void FolderHeaderView::SetFolder(const SidebarFolder& folder) {
  // Headers are pooled by position, so this slot can be handed a different
  // folder at any time. An open field belongs to the folder it was opened on.
  if (is_renaming() && folder.id != renaming_folder_id_) {
    AbandonRename();
  }
  folder_ = folder;
  UpdateVisuals();
}

void FolderHeaderView::UpdateVisuals() {
  disclosure_->SetImage(ui::ImageModel::FromVectorIcon(
      folder_.collapsed ? kDisclosureCollapsedIcon : kDisclosureExpandedIcon,
      kColorArciumRowTextSecondary, metrics::kFaviconSize));
  const bool renaming = is_renaming();
  name_->SetVisible(!renaming);
  name_->SetText(folder_.name);
  name_->SetEnabledColor(kColorArciumRowText);
  count_->SetVisible(!renaming);
  count_->SetText(base::NumberToString16(folder_.entry_count));
  count_->SetEnabledColor(kColorArciumRowTextSecondary);
  GetViewAccessibility().SetName(folder_.name);
  OnThemeChanged();
}

void FolderHeaderView::Toggle() {
  if (is_renaming() || !delegate_.toggle_collapsed) {
    return;
  }
  // Copies: toggling rebuilds the list and can destroy this view.
  base::RepeatingCallback<void(const SidebarFolder&)> toggle =
      delegate_.toggle_collapsed;
  const SidebarFolder folder = folder_;
  toggle.Run(folder);
}

void FolderHeaderView::BeginRename() {
  if (is_renaming()) {
    return;
  }
  renaming_folder_id_ = folder_.id;
  auto field = std::make_unique<RenameField>(
      folder_.name,
      base::BindOnce(&FolderHeaderView::OnRenameFinished,
                     weak_factory_.GetWeakPtr(), renaming_folder_id_));
  field->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  rename_field_ = AddChildViewAt(std::move(field), GetIndexOf(name_).value());
  // See TabRowView: a focusable view without a name cannot be announced.
  rename_field_->GetViewAccessibility().SetName(u"Folder name");
  name_->SetVisible(false);
  count_->SetVisible(false);
  rename_field_->RequestFocus();
  InvalidateLayout();
}

void FolderHeaderView::AbandonRename() {
  if (!rename_field_) {
    return;
  }
  RenameField* field = rename_field_.ExtractAsDangling();
  field->Abandon();
  renaming_folder_id_ = FolderId();
  RemoveChildViewT(field);
  name_->SetVisible(true);
  count_->SetVisible(true);
  InvalidateLayout();
}

void FolderHeaderView::OnRenameFinished(FolderId id,
                                        bool commit,
                                        const std::u16string& name) {
  if (rename_field_) {
    RemoveChildViewT(rename_field_.ExtractAsDangling());
    renaming_folder_id_ = FolderId();
  }
  name_->SetVisible(true);
  count_->SetVisible(true);
  InvalidateLayout();
  // `id` is the folder the edit was started on; see TabRowView.
  if (!commit || name.empty() || !id.is_valid() || !delegate_.rename) {
    return;
  }
  base::RepeatingCallback<void(FolderId, const std::u16string&)> rename =
      delegate_.rename;
  rename.Run(id, name);
}

bool FolderHeaderView::OnKeyPressed(const ui::KeyEvent& event) {
  // F2 renames, the conventional key for renaming a thing in place. This
  // header is FocusBehavior::ACCESSIBLE_ONLY, so the key only reaches here
  // once the header has the focus, which requires the focus manager's
  // keyboard-accessible mode — the state macOS Full Keyboard Access and
  // VoiceOver turn on (View::RequestFocusWithReason gates a request on
  // IsAccessibilityFocusable() in that mode; see ui/views/view.cc). The
  // binding is live for those users today; it is not reachable through plain
  // Tab traversal, which the context menu's Rename covers for everyone else.
  // Collapse stays on the single click: delaying it by the double-click
  // interval to free up a double click would make the common gesture feel
  // laggy to serve a rare one.
  if (event.key_code() == ui::VKEY_F2) {
    BeginRename();
    return true;
  }
  return views::Button::OnKeyPressed(event);
}

void FolderHeaderView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  OnThemeChanged();
}

void FolderHeaderView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  OnThemeChanged();
}

void FolderHeaderView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!delegate_.show_context_menu || is_renaming()) {
    return;
  }
  delegate_.show_context_menu.Run(this, folder_, point);
}

void FolderHeaderView::OnThemeChanged() {
  views::Button::OnThemeChanged();
  if (drop_target_) {
    // The accent tint, not the hover grey: a drop is a commitment and has to
    // read as more than the pointer passing over.
    SetBackground(views::CreateRoundedRectBackground(
        kColorArciumSpaceChipActiveBackground, metrics::kRowCornerRadius));
  } else if (hovered_) {
    SetBackground(views::CreateRoundedRectBackground(
        kColorArciumRowHoverBackground, metrics::kRowCornerRadius));
  } else {
    SetBackground(nullptr);
  }
}

void FolderHeaderView::SetDropTarget(bool drop_target) {
  if (drop_target_ == drop_target) {
    return;
  }
  drop_target_ = drop_target;
  OnThemeChanged();
}

bool FolderHeaderView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  format_types->insert(RowDragData::Format());
  return true;
}

bool FolderHeaderView::AreDropTypesRequired() {
  return true;
}

void FolderHeaderView::WriteDragDataForView(views::View* sender,
                                            const gfx::Point& press_pt,
                                            ui::OSExchangeData* data) {
  RowDragData payload;
  payload.folder_id = folder_.id;
  payload.Write(data);
  // The image helper draws a title and, with no icon to rasterize, the
  // default favicon. A folder has neither an icon nor a URL, so it gets
  // exactly that with its own name on it -- rather than a second helper that
  // would draw the same thing.
  SidebarRow as_row;
  as_row.title = folder_.name;
  SetRowDragImage(as_row, sender, press_pt, data);
  // Once per drag, at the one moment a source knows one is starting.
  if (delegate_.drag_started) {
    delegate_.drag_started.Run();
  }
}

int FolderHeaderView::GetDragOperationsForView(views::View* sender,
                                               const gfx::Point& p) {
  // While a rename is open the field owns the header, and a header drawing no
  // folder is not something anything can be told to move.
  if (is_renaming() || !folder_.id.is_valid()) {
    return ui::DragDropTypes::DRAG_NONE;
  }
  // Never DRAG_COPY: a folder is one thing in one place, and two headers for
  // one folder is a state the model cannot hold.
  return ui::DragDropTypes::DRAG_MOVE;
}

bool FolderHeaderView::CanStartDragForView(views::View* sender,
                                           const gfx::Point& press_pt,
                                           const gfx::Point& p) {
  if (is_renaming()) {
    return false;
  }
  // Views' own threshold, so a header starts dragging exactly when every
  // other draggable view does.
  return views::View::ExceededDragThreshold(press_pt - p);
}

bool FolderHeaderView::CanAccept(const ui::OSExchangeData& data) const {
  std::optional<RowDragData> payload = RowDragData::Read(data);
  if (!payload) {
    return false;
  }
  if (payload->is_folder()) {
    // Into itself, into its own descendant, or past the depth cap: all three
    // are properties of the tree, so the owning list answers them.
    return delegate_.can_accept_folder &&
           delegate_.can_accept_folder.Run(payload->folder_id, folder_.id);
  }
  // Entries only otherwise, and only entries a folder may hold. A Today tab
  // has no entry to put in a folder; a favourite is a tile in the grid with
  // nowhere to be indented to. Refusing either here is what lets DropHelper
  // walk up to the Pinned list -- for a tab that is how it becomes an entry.
  return payload->is_entry() && delegate_.can_accept_entry &&
         delegate_.can_accept_entry.Run(payload->entry_id);
}

bool FolderHeaderView::CanDrop(const ui::OSExchangeData& data) {
  return CanAccept(data);
}

void FolderHeaderView::OnDragEntered(const ui::DropTargetEvent& event) {
  SetDropTarget(true);
}

int FolderHeaderView::OnDragUpdated(const ui::DropTargetEvent& event) {
  // No per-event work: the whole header is one target, so where inside it the
  // pointer is does not change the answer.
  return ui::DragDropTypes::DRAG_MOVE;
}

void FolderHeaderView::OnDragExited() {
  SetDropTarget(false);
}

views::View::DropCallback FolderHeaderView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  SetDropTarget(false);
  std::optional<RowDragData> payload = RowDragData::Read(event.data());
  if (!payload || !CanAccept(event.data())) {
    return base::NullCallback();
  }
  if (payload->is_folder() ? !delegate_.drop_folder : !delegate_.drop_entry) {
    return base::NullCallback();
  }
  return base::BindOnce(&FolderHeaderView::PerformDrop,
                        weak_factory_.GetWeakPtr(), *payload);
}

void FolderHeaderView::PerformDrop(
    RowDragData payload,
    const ui::DropTargetEvent& event,
    ui::mojom::DragOperation& output_drag_op,
    std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
  output_drag_op = ui::mojom::DragOperation::kMove;
  // Copies of the folder and of the callback: the command rebuilds the list
  // and can destroy this view before Run() returns.
  const SidebarFolder folder = folder_;
  if (payload.is_folder()) {
    base::RepeatingCallback<void(FolderId, const SidebarFolder&)> drop =
        delegate_.drop_folder;
    drop.Run(payload.folder_id, folder);
    return;
  }
  base::RepeatingCallback<void(EntryId, const SidebarFolder&)> drop =
      delegate_.drop_entry;
  drop.Run(payload.entry_id, folder);
}

gfx::Size FolderHeaderView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(metrics::kSidebarWidth),
                   metrics::kRowHeight);
}

BEGIN_METADATA(FolderHeaderView)
END_METADATA

}  // namespace arcium
