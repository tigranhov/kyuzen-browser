// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
#define ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_

#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/row_drag_session.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"

namespace ui {
class ClipboardFormatType;
class OSExchangeData;
}  // namespace ui

namespace views {
class ImageButton;
}

namespace arcium {

class RenameField;
class RowContextMenu;

// Square tiles, four per row, one per row of section kFavorites. The grid is
// the tiles' context menu controller rather than each tile being its own: a
// tile is a plain views::ImageButton with no idea which row it draws, and the
// grid already holds that mapping.
class FavoritesGridView : public views::View,
                          public views::ContextMenuController,
                          public views::DragController,
                          public RowDragSession::Observer {
  METADATA_HEADER(FavoritesGridView, views::View)

 public:
  explicit FavoritesGridView(SidebarModel* model);
  FavoritesGridView(const FavoritesGridView&) = delete;
  FavoritesGridView& operator=(const FavoritesGridView&) = delete;
  ~FavoritesGridView() override;

  void SetRows(const std::vector<SidebarRow>& rows);

  // The shared "a sidebar row is being dragged" signal. The grid is both a
  // source and, while it holds no tiles, a target that only exists during a
  // drag. Null detaches; see TabListView::SetDragSession.
  void SetDragSession(RowDragSession* session);

  // Where the gap indicator sits, or nothing when no drag is over the grid.
  // An index into the tiles; tile count means "after the last one".
  std::optional<size_t> drop_index_for_testing() const { return drop_index_; }
  // The tile drawn for `rows()[index]`, so a test can ask what it painted --
  // the same reason TabRowView exposes its own children this way.
  views::ImageButton* tile_at_for_testing(size_t index) {
    return tiles_[index];
  }

  // views::View:
  void Layout(PassKey) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool AreDropTypesRequired() override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  void OnDragEntered(const ui::DropTargetEvent& event) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // views::DragController. The grid is its tiles' drag controller for the
  // same reason it is their context menu controller: a tile is a plain
  // ImageButton with no idea which row it draws, and the grid holds that map.
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override;

  // RowDragSession::Observer:
  void OnRowDragInFlightChanged() override;

 private:
  void OnTileActivated(EntryId entry_id, int tab_index);

  // The tile `sender` is, or nothing when it is not one of ours.
  std::optional<size_t> IndexOfTile(const views::View* sender) const;
  // Where in the tiles a drop at `p` would insert: 0..tiles_.size().
  size_t DropTileIndex(const gfx::Point& p) const;
  // The entry the drop lands before, or an invalid id for the end of the
  // grid. Tiles are pooled by index and another window on the same profile
  // can rebuild this grid inside the drag's nested loop, so a slot outlives
  // what it holds and an index is not a safe thing to carry to the drop.
  EntryId AnchorForDropIndex(size_t index) const;
  // That anchor read back against the grid as it now stands.
  int PositionForAnchor(EntryId before) const;
  // Where `id` sits among the tiles now, or nothing when it is not one.
  std::optional<size_t> IndexOfEntry(EntryId id) const;
  // While a row drag is running an empty grid reserves a band to aim at:
  // hidden, it is skipped by GetEventHandlerForPoint, and "Today tab ->
  // Favourites" is unreachable on a fresh profile. Only while one is running,
  // so idle layout does not move.
  bool ReservesDropBand() const;
  // The gap the indicator fills for `index`, in this view's coordinates.
  gfx::Rect DropIndicatorBounds(size_t index) const;
  void SetDropIndex(std::optional<size_t> index);
  void PerformDrop(RowDragData payload,
                   EntryId anchor,
                   const ui::DropTargetEvent& event,
                   ui::mojom::DragOperation& output_drag_op,
                   std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner);

  // Swaps the tile at `index`'s whole row for a RenameField bounded to that
  // row — a tile is a quarter of the sidebar wide and has nowhere to host a
  // field on its own, but the row it sits in does. Mirrors
  // TabRowView::BeginRename: captures the entry id at the point the edit
  // actually starts, not when the menu closure was built.
  void BeginRenameForTile(size_t index);
  // Shows or hides every tile in the same grid row as `index`. The field is
  // bounded to the whole row, so the row's other tiles would otherwise sit
  // underneath it — still visible, still at their own columns, and unable to
  // take the clicks they look able to take, because the field is added last
  // and Views hit-tests front to back.
  void SetRowTilesVisible(size_t index, bool visible);
  bool is_renaming() const { return rename_field_ != nullptr; }
  // Takes the field away without an outcome, for when the tile pool is
  // re-pointed at a different entry out from under an open rename.
  void AbandonRename();
  void OnRenameFinished(EntryId id, bool commit, const std::u16string& text);

  raw_ptr<SidebarModel> model_;
  std::vector<raw_ptr<views::ImageButton>> tiles_;
  // Parallel to `tiles_`: what each tile was last drawn from, which is what
  // its menu is built for. A snapshot, like every other menu in the sidebar.
  std::vector<SidebarRow> rows_;
  // Outlives the menu it is running; see TabListView.
  std::unique_ptr<RowContextMenu> context_menu_;
  raw_ptr<RenameField> rename_field_ = nullptr;
  // Captured when the edit starts. Tiles are pooled by position exactly like
  // TabRowView's rows, so a slot can be handed a different entry at any time.
  EntryId renaming_entry_id_;
  size_t renaming_tile_index_ = 0;
  // Read once when a drag enters and reused for every move over the grid; see
  // TabListView for why.
  std::optional<RowDragData> drag_payload_;
  std::optional<size_t> drop_index_;
  base::ScopedObservation<RowDragSession, RowDragSession::Observer>
      drag_session_{this};
  base::WeakPtrFactory<FavoritesGridView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
