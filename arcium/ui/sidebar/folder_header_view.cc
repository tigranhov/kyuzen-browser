// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/folder_header_view.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
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
  SetBackground(
      hovered_ ? views::CreateRoundedRectBackground(
                     kColorArciumRowHoverBackground, metrics::kRowCornerRadius)
               : nullptr);
}

gfx::Size FolderHeaderView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(metrics::kSidebarWidth),
                   metrics::kRowHeight);
}

BEGIN_METADATA(FolderHeaderView)
END_METADATA

}  // namespace arcium
