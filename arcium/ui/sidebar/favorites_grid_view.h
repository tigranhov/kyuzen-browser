// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
#define ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_

#include <memory>
#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

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
                          public views::ContextMenuController {
  METADATA_HEADER(FavoritesGridView, views::View)

 public:
  explicit FavoritesGridView(SidebarModel* model);
  FavoritesGridView(const FavoritesGridView&) = delete;
  FavoritesGridView& operator=(const FavoritesGridView&) = delete;
  ~FavoritesGridView() override;

  void SetRows(const std::vector<SidebarRow>& rows);

  // views::View:
  void Layout(PassKey) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  void OnTileActivated(EntryId entry_id, int tab_index);

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
  base::WeakPtrFactory<FavoritesGridView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
