// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_
#define ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/controls/button/button.h"

namespace views {
class ImageButton;
class ImageView;
class Label;
class Throbber;
}  // namespace views

namespace arcium {

// One 32px row: favicon or throbber, title, audio indicator, hover close.
class TabRowView : public views::Button {
  METADATA_HEADER(TabRowView, views::Button)

 public:
  struct Delegate {
    base::RepeatingCallback<void(int tab_index)> activate;
    base::RepeatingCallback<void(int tab_index)> close;
    // Called while dragging: the row at `from` wants to move to `to`.
    base::RepeatingCallback<void(int from, int to)> drag_move;
  };

  explicit TabRowView(Delegate delegate);
  TabRowView(const TabRowView&) = delete;
  TabRowView& operator=(const TabRowView&) = delete;
  ~TabRowView() override;

  void SetRow(const SidebarRow& row);
  int tab_index() const { return row_.tab_index; }

  // views::Button / View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnThemeChanged() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  void UpdateVisuals();
  void UpdateCloseButtonVisibility();

  Delegate delegate_;
  SidebarRow row_;
  bool hovered_ = false;
  bool dragging_ = false;
  gfx::Point drag_start_;

  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Throbber> throbber_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageView> audio_ = nullptr;
  raw_ptr<views::ImageButton> close_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_
