// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/favorites_grid_view.h"

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"

namespace arcium {

namespace {

int TileSize(int width) {
  const int gaps = (metrics::kFavoritesPerRow - 1) * metrics::kFavoriteTileGap;
  return (width - gaps) / metrics::kFavoritesPerRow;
}

// The full-width strip a tile's row occupies — where the rename field goes,
// rather than the one tile, which is a quarter of the sidebar wide and has
// nowhere to put one.
gfx::Rect TileRowBounds(int width, size_t index) {
  const int size = TileSize(width);
  const int row = static_cast<int>(index) / metrics::kFavoritesPerRow;
  return gfx::Rect(0, row * (size + metrics::kFavoriteTileGap), width, size);
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
  // Tiles are pooled by position exactly like TabRowView's rows: this walk
  // can hand the renaming slot's index a different entry. An open field
  // belongs to the entry it was opened on, not to the slot.
  if (is_renaming() &&
      (renaming_tile_index_ >= mine.size() ||
       mine[renaming_tile_index_]->entry_id != renaming_entry_id_)) {
    AbandonRename();
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
  // Every tile is shown except the row an open rename covers. The pool both
  // makes new tiles (which start visible) and hands old ones back (which keep
  // whatever a previous rename left them at), so neither end of it can be
  // trusted to have got this right on its own.
  for (const raw_ptr<views::ImageButton>& tile : tiles_) {
    tile->SetVisible(true);
  }
  if (is_renaming()) {
    SetRowTilesVisible(renaming_tile_index_, false);
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
      // The Rename item starts the edit on this tile's row; see
      // BeginRenameForTile for why it is bounded to the row rather than the
      // tile.
      context_menu_->RunForRow(
          rows_[i], source, point,
          base::BindRepeating(&FavoritesGridView::BeginRenameForTile,
                              weak_factory_.GetWeakPtr(), i));
      return;
    }
  }
}

void FavoritesGridView::BeginRenameForTile(size_t index) {
  if (is_renaming() || index >= tiles_.size() || index >= rows_.size()) {
    return;
  }
  const SidebarRow& row = rows_[index];
  // A favourite is always an entry (see OnTileActivated), but a stale index
  // handed to a posted closure is worth guarding the same way BeginRename
  // does for a Today row.
  if (!row.entry_id.is_valid()) {
    return;
  }
  renaming_entry_id_ = row.entry_id;
  renaming_tile_index_ = index;
  auto field = std::make_unique<RenameField>(
      row.title,
      base::BindOnce(&FavoritesGridView::OnRenameFinished,
                     weak_factory_.GetWeakPtr(), renaming_entry_id_));
  rename_field_ = AddChildView(std::move(field));
  rename_field_->GetViewAccessibility().SetName(u"Favorite name");
  SetRowTilesVisible(index, false);
  rename_field_->SetBoundsRect(TileRowBounds(width(), index));
  rename_field_->RequestFocus();
}

void FavoritesGridView::SetRowTilesVisible(size_t index, bool visible) {
  const size_t per_row = static_cast<size_t>(metrics::kFavoritesPerRow);
  const size_t first = (index / per_row) * per_row;
  for (size_t i = first; i < first + per_row && i < tiles_.size(); ++i) {
    tiles_[i]->SetVisible(visible);
  }
}

void FavoritesGridView::AbandonRename() {
  if (!rename_field_) {
    return;
  }
  RenameField* field = rename_field_.ExtractAsDangling();
  field->Abandon();
  renaming_entry_id_ = EntryId();
  RemoveChildViewT(field);
  SetRowTilesVisible(renaming_tile_index_, true);
}

void FavoritesGridView::OnRenameFinished(EntryId id,
                                         bool commit,
                                         const std::u16string& title) {
  if (rename_field_) {
    RemoveChildViewT(rename_field_.ExtractAsDangling());
    renaming_entry_id_ = EntryId();
    SetRowTilesVisible(renaming_tile_index_, true);
  }
  // `id` is the entry the edit was started on, captured when it began, not
  // whatever this pooled tile draws now; see TabRowView::OnRenameFinished.
  if (!commit || title.empty() || !id.is_valid()) {
    return;
  }
  model_->SetEntryTitle(id, title);
}

void FavoritesGridView::Layout(PassKey) {
  const int size = TileSize(width());
  for (size_t i = 0; i < tiles_.size(); ++i) {
    const int col = i % metrics::kFavoritesPerRow;
    const int row = i / metrics::kFavoritesPerRow;
    tiles_[i]->SetBounds(col * (size + metrics::kFavoriteTileGap),
                         row * (size + metrics::kFavoriteTileGap), size, size);
  }
  if (rename_field_) {
    rename_field_->SetBoundsRect(TileRowBounds(width(), renaming_tile_index_));
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
