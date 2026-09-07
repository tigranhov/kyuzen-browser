// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/favorites_grid_view.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"

namespace arcium {

namespace {

int TileSize(int width) {
  const int gaps = (metrics::kFavoritesPerRow - 1) * metrics::kFavoriteTileGap;
  return (width - gaps) / metrics::kFavoritesPerRow;
}

}  // namespace

FavoritesGridView::FavoritesGridView(SidebarModel* model) : model_(model) {}

FavoritesGridView::~FavoritesGridView() = default;

void FavoritesGridView::SetRows(const std::vector<SidebarRow>& rows) {
  std::vector<const SidebarRow*> mine;
  for (const SidebarRow& row : rows) {
    if (row.section == SidebarSection::kFavorites) {
      mine.push_back(&row);
    }
  }
  while (tiles_.size() < mine.size()) {
    auto tile = std::make_unique<views::ImageButton>();
    tile->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    tile->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    // Right-click reaches the same menu a pinned row's does; without this the
    // only way to unpin a favourite would be drag and drop, which is Task 8.
    tile->set_context_menu_controller(this);
    tiles_.push_back(AddChildView(std::move(tile)));
  }
  while (tiles_.size() > mine.size()) {
    RemoveChildViewT(tiles_.back().get());
    tiles_.pop_back();
  }
  rows_.clear();
  for (size_t i = 0; i < mine.size(); ++i) {
    const SidebarRow& row = *mine[i];
    rows_.push_back(row);
    tiles_[i]->SetImageModel(views::Button::STATE_NORMAL, row.favicon);
    tiles_[i]->SetTooltipText(row.title);
    tiles_[i]->GetViewAccessibility().SetName(row.title);
    tiles_[i]->SetCallback(base::BindRepeating(
        &FavoritesGridView::OnTileActivated, base::Unretained(this),
        row.entry_id, row.tab_index));
    tiles_[i]->SetBackground(views::CreateRoundedRectBackground(
        row.is_active ? kColorArciumRowActiveBackground
                      : kColorArciumControlBackground,
        metrics::kRowCornerRadius));
  }
  SetVisible(!tiles_.empty());
  InvalidateLayout();
}

void FavoritesGridView::OnTileActivated(EntryId entry_id, int tab_index) {
  // A favourite is an entry, so it is normally the first branch; the fallback
  // exists for the playground's fake, whose rows carry no entry.
  if (entry_id.is_valid()) {
    model_->ActivateEntry(entry_id);
  } else {
    model_->ActivateTab(tab_index);
  }
}

void FavoritesGridView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  for (size_t i = 0; i < tiles_.size() && i < rows_.size(); ++i) {
    if (tiles_[i] == source) {
      context_menu_ = std::make_unique<RowContextMenu>(model_);
      // No rename closure: a 40px tile has nowhere to put a field, so the
      // menu's Rename comes back disabled rather than doing nothing.
      context_menu_->RunForRow(rows_[i], source, point,
                               base::RepeatingClosure());
      return;
    }
  }
}

void FavoritesGridView::Layout(PassKey) {
  const int size = TileSize(width());
  for (size_t i = 0; i < tiles_.size(); ++i) {
    const int col = i % metrics::kFavoritesPerRow;
    const int row = i / metrics::kFavoritesPerRow;
    tiles_[i]->SetBounds(col * (size + metrics::kFavoriteTileGap),
                         row * (size + metrics::kFavoriteTileGap), size, size);
  }
}

gfx::Size FavoritesGridView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().value_or(
      metrics::kSidebarWidth - 2 * metrics::kSidebarPadding);
  const int rows =
      (static_cast<int>(tiles_.size()) + metrics::kFavoritesPerRow - 1) /
      metrics::kFavoritesPerRow;
  const int size = TileSize(width);
  return gfx::Size(
      width,
      rows == 0 ? 0 : rows * size + (rows - 1) * metrics::kFavoriteTileGap);
}

BEGIN_METADATA(FavoritesGridView)
END_METADATA

}  // namespace arcium
