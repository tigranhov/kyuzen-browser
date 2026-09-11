// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/favorites_grid_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "arcium/browser/model/reorder_index.h"
#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/row_drag_image.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/unloaded_row_dimming.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/canvas.h"
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

// The gap indicator's width. It fills the gap between two tiles rather than
// drawing a hairline in it, so it reads as "the tile goes here".
constexpr int kDropIndicatorWidth = 3;

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
    // Right-click reaches the same menu a pinned row's does.
    tile->set_context_menu_controller(this);
    tile->set_drag_controller(this);
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
    // Favourites carry no title of their own, so the icon is the only place a
    // cold one can be told apart from a loaded one at a glance.
    tiles_[i]->SetImageModel(
        views::Button::STATE_NORMAL,
        row.needs_load() ? DimUnloadedFavicon(row.favicon) : row.favicon);
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
  SetVisible(!tiles_.empty() || ReservesDropBand());
  InvalidateLayout();
}

void FavoritesGridView::SetDragSession(RowDragSession* session) {
  drag_session_.Reset();
  if (session) {
    drag_session_.Observe(session);
  }
  OnRowDragInFlightChanged();
}

void FavoritesGridView::OnRowDragInFlightChanged() {
  SetVisible(!tiles_.empty() || ReservesDropBand());
  PreferredSizeChanged();
}

bool FavoritesGridView::ReservesDropBand() const {
  return tiles_.empty() && drag_session_.IsObserving() &&
         drag_session_.GetSource()->in_flight();
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

std::optional<size_t> FavoritesGridView::IndexOfTile(
    const views::View* sender) const {
  for (size_t i = 0; i < tiles_.size() && i < rows_.size(); ++i) {
    if (tiles_[i] == sender) {
      return i;
    }
  }
  return std::nullopt;
}

void FavoritesGridView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  std::optional<size_t> index = IndexOfTile(source);
  if (!index) {
    return;
  }
  context_menu_ = std::make_unique<RowContextMenu>(model_);
  // The Rename item starts the edit on this tile's row; see
  // BeginRenameForTile for why it is bounded to the row rather than the tile.
  context_menu_->RunForRow(
      rows_[*index], source, point,
      base::BindRepeating(&FavoritesGridView::BeginRenameForTile,
                          weak_factory_.GetWeakPtr(), *index));
}

void FavoritesGridView::WriteDragDataForView(views::View* sender,
                                             const gfx::Point& press_pt,
                                             ui::OSExchangeData* data) {
  std::optional<size_t> index = IndexOfTile(sender);
  if (!index) {
    return;
  }
  RowDragData payload;
  payload.entry_id = rows_[*index].entry_id;
  payload.tab_index = rows_[*index].tab_index;
  payload.Write(data);
  SetRowDragImage(rows_[*index], sender, press_pt, data);
  // Once per drag, at the one moment a source knows a drag is starting: the
  // empty sections need a band to be droppable at all.
  if (drag_session_.IsObserving()) {
    drag_session_.GetSource()->Begin(GetWidget());
  }
}

int FavoritesGridView::GetDragOperationsForView(views::View* sender,
                                                const gfx::Point& p) {
  std::optional<size_t> index = IndexOfTile(sender);
  if (is_renaming() || !index ||
      (!rows_[*index].entry_id.is_valid() && rows_[*index].tab_index < 0)) {
    return ui::DragDropTypes::DRAG_NONE;
  }
  return ui::DragDropTypes::DRAG_MOVE;
}

bool FavoritesGridView::CanStartDragForView(views::View* sender,
                                            const gfx::Point& press_pt,
                                            const gfx::Point& p) {
  return !is_renaming() && views::View::ExceededDragThreshold(press_pt - p);
}

size_t FavoritesGridView::DropTileIndex(const gfx::Point& p) const {
  const int pitch = TileSize(width()) + metrics::kFavoriteTileGap;
  if (pitch <= 0 || tiles_.empty()) {
    return tiles_.size();
  }
  const int grid_row = std::max(0, p.y() / pitch);
  // Half a pitch across: the nearest gap, not the tile the pointer is over.
  const int column =
      std::clamp((p.x() + pitch / 2) / pitch, 0, metrics::kFavoritesPerRow);
  const size_t index = static_cast<size_t>(grid_row) *
                           static_cast<size_t>(metrics::kFavoritesPerRow) +
                       static_cast<size_t>(column);
  return std::min(index, tiles_.size());
}

EntryId FavoritesGridView::AnchorForDropIndex(size_t index) const {
  return index < rows_.size() ? rows_[index].entry_id : EntryId();
}

int FavoritesGridView::PositionForAnchor(EntryId before) const {
  if (before.is_valid()) {
    for (size_t i = 0; i < rows_.size(); ++i) {
      if (rows_[i].entry_id == before) {
        return static_cast<int>(i);
      }
    }
  }
  // No anchor, or an anchor the model dropped while the nested loop was
  // running: the end of the grid, which is the one place that is still there.
  return static_cast<int>(rows_.size());
}

std::optional<size_t> FavoritesGridView::IndexOfEntry(EntryId id) const {
  if (!id.is_valid()) {
    return std::nullopt;
  }
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].entry_id == id) {
      return i;
    }
  }
  return std::nullopt;
}

gfx::Rect FavoritesGridView::DropIndicatorBounds(size_t index) const {
  const int size = TileSize(width());
  const int pitch = size + metrics::kFavoriteTileGap;
  const size_t per_row = static_cast<size_t>(metrics::kFavoritesPerRow);
  // Past the last tile, the indicator goes after it rather than at the start
  // of a row that does not exist yet.
  size_t slot = index;
  bool trailing = false;
  if (index >= tiles_.size() && !tiles_.empty()) {
    slot = tiles_.size() - 1;
    trailing = true;
  }
  const int grid_row = static_cast<int>(slot / per_row);
  const int column = static_cast<int>(slot % per_row) + (trailing ? 1 : 0);
  // Centred on the gap before `column`; column 0 has no gap to its left, so
  // it sits flush against the grid's edge.
  const int x =
      std::max(0, column * pitch -
                      (metrics::kFavoriteTileGap + kDropIndicatorWidth) / 2);
  return gfx::Rect(std::min(x, std::max(0, width() - kDropIndicatorWidth)),
                   grid_row * pitch, kDropIndicatorWidth, size);
}

void FavoritesGridView::SetDropIndex(std::optional<size_t> index) {
  if (drop_index_ == index) {
    return;
  }
  drop_index_ = index;
  SchedulePaint();
}

bool FavoritesGridView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  format_types->insert(RowDragData::Format());
  return true;
}

bool FavoritesGridView::AreDropTypesRequired() {
  return true;
}

bool FavoritesGridView::CanDrop(const ui::OSExchangeData& data) {
  const std::optional<RowDragData> payload = RowDragData::Read(data);
  // A folder is a list's row, and a favourite is a tile: there is nowhere in
  // this grid for a folder to land. Refusing here rather than at the drop is
  // what keeps the gap indicator honest -- accepting would open a gap, follow
  // the pointer, and then quietly do nothing, which is an affordance that
  // lies about what a release will do.
  return payload.has_value() && !payload->is_folder();
}

void FavoritesGridView::OnDragEntered(const ui::DropTargetEvent& event) {
  drag_payload_ = RowDragData::Read(event.data());
}

int FavoritesGridView::OnDragUpdated(const ui::DropTargetEvent& event) {
  if (!drag_payload_) {
    drag_payload_ = RowDragData::Read(event.data());
  }
  if (!drag_payload_) {
    SetDropIndex(std::nullopt);
    return ui::DragDropTypes::DRAG_NONE;
  }
  SetDropIndex(DropTileIndex(event.location()));
  return ui::DragDropTypes::DRAG_MOVE;
}

void FavoritesGridView::OnDragExited() {
  drag_payload_.reset();
  SetDropIndex(std::nullopt);
}

views::View::DropCallback FavoritesGridView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  std::optional<RowDragData> payload = drag_payload_;
  if (!payload) {
    payload = RowDragData::Read(event.data());
  }
  const EntryId anchor = AnchorForDropIndex(DropTileIndex(event.location()));
  drag_payload_.reset();
  SetDropIndex(std::nullopt);
  if (!payload) {
    return base::NullCallback();
  }
  return base::BindOnce(&FavoritesGridView::PerformDrop,
                        weak_factory_.GetWeakPtr(), *payload, anchor);
}

void FavoritesGridView::PerformDrop(
    RowDragData payload,
    EntryId anchor,
    const ui::DropTargetEvent& event,
    ui::mojom::DragOperation& output_drag_op,
    std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
  output_drag_op = ui::mojom::DragOperation::kMove;
  const int to = PositionForAnchor(anchor);
  if (payload.is_entry()) {
    // Favourites have no folders, so a tile's index is the position the model
    // orders by; reordering inside the grid is the same command as arriving
    // from Pinned, with the kind change turning into a no-op.
    //
    // The gap the tile was dropped in counts the grid as it looks now, with
    // the dragged tile still in it, while MoveEntryToSection is
    // lift-then-insert. In a grid the shift reads as "one place left" rather
    // than "one place up", but it is the same rule and the same function; a
    // tile arriving from Pinned is not in this count, which is the nullopt
    // case.
    const std::optional<size_t> from = IndexOfEntry(payload.entry_id);
    model_->MoveEntryToSection(
        payload.entry_id, SidebarSection::kFavorites,
        LiftThenInsertIndex(
            from ? std::optional<int>(static_cast<int>(*from)) : std::nullopt,
            to));
    return;
  }
  // A Today tab becomes a favourite where the gap indicator was drawn, not at
  // the end: the indicator promised a place before the gesture was taken.
  model_->MoveTabToSection(payload.tab_index, SidebarSection::kFavorites, to);
}

void FavoritesGridView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (!drop_index_) {
    return;
  }
  canvas->FillRect(DropIndicatorBounds(*drop_index_),
                   GetColorProvider()->GetColor(kColorArciumSpaceAccent));
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
  if (rows == 0) {
    // One tile row while a drag is running, nothing at all otherwise: an
    // empty grid must be droppable without changing what the sidebar looks
    // like at rest.
    return gfx::Size(width, ReservesDropBand() ? size : 0);
  }
  return gfx::Size(width, rows * size + (rows - 1) * metrics::kFavoriteTileGap);
}

BEGIN_METADATA(FavoritesGridView)
END_METADATA

}  // namespace arcium
