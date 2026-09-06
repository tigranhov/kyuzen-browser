// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
#define ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class ImageButton;
}

namespace arcium {

// Square tiles, four per row, one per row of section kFavorites.
class FavoritesGridView : public views::View {
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

 private:
  raw_ptr<SidebarModel> model_;
  std::vector<raw_ptr<views::ImageButton>> tiles_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
