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
  auto field = std::make_unique<RenameField>(
      folder_.name, base::BindOnce(&FolderHeaderView::OnRenameFinished,
                                   weak_factory_.GetWeakPtr()));
  field->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  rename_field_ = AddChildViewAt(std::move(field), GetIndexOf(name_).value());
  name_->SetVisible(false);
  count_->SetVisible(false);
  rename_field_->RequestFocus();
  InvalidateLayout();
}

void FolderHeaderView::OnRenameFinished(bool commit,
                                        const std::u16string& name) {
  if (rename_field_) {
    RemoveChildViewT(rename_field_.ExtractAsDangling());
  }
  name_->SetVisible(true);
  count_->SetVisible(true);
  InvalidateLayout();
  if (!commit || name.empty() || !delegate_.rename) {
    return;
  }
  base::RepeatingCallback<void(const SidebarFolder&, const std::u16string&)>
      rename = delegate_.rename;
  const SidebarFolder folder = folder_;
  rename.Run(folder, name);
}

bool FolderHeaderView::OnMousePressed(const ui::MouseEvent& event) {
  // The second press of a double click renames instead of toggling again.
  // The first press has already toggled; a header that flips once on the way
  // into a rename is a smaller surprise than a rename that needs a menu.
  if ((event.flags() & ui::EF_IS_DOUBLE_CLICK) &&
      event.IsOnlyLeftMouseButton()) {
    BeginRename();
    return true;
  }
  return views::Button::OnMousePressed(event);
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
