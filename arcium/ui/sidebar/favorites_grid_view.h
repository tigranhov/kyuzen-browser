// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
#define ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_

#include <memory>
#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

namespace views {
class ImageButton;
}

namespace arcium {

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

  raw_ptr<SidebarModel> model_;
  std::vector<raw_ptr<views::ImageButton>> tiles_;
  // Parallel to `tiles_`: what each tile was last drawn from, which is what
  // its menu is built for. A snapshot, like every other menu in the sidebar.
  std::vector<SidebarRow> rows_;
  // Outlives the menu it is running; see TabListView.
  std::unique_ptr<RowContextMenu> context_menu_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
