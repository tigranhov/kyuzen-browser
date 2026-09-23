// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SPLIT_GRIP_VIEW_H_
#define ARCIUM_UI_SIDEBAR_SPLIT_GRIP_VIEW_H_

#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/functional/callback.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"

namespace arcium {

// The handle between the two halves of a split row. Dragging it carries the
// pair, which keeps the split and moves it; dragging either half pulls that
// half out and ends it. Drawn only while the pointer is over the row, and
// sitting in the gap the row leaves between its halves, so it takes nothing
// from either one.
class SplitGripView : public views::View, public views::DragController {
  METADATA_HEADER(SplitGripView, views::View)

 public:
  struct Delegate {
    // A drag of the pair is starting; the same signal a row sends.
    base::RepeatingCallback<void(const RowDragData& payload)> drag_started;
    // The pointer came onto the grip or left it.
    base::RepeatingClosure hover_changed;
  };

  explicit SplitGripView(Delegate delegate);
  SplitGripView(const SplitGripView&) = delete;
  SplitGripView& operator=(const SplitGripView&) = delete;
  ~SplitGripView() override;

  // The two halves, in pane order. The pair is named by its entry half when
  // it has one, because an entry outlives its tab and a pinned pair moves
  // through its entries; a pair of Today tabs is named by its first tab.
  void SetPair(const SidebarRow& first, const SidebarRow& second);
  const RowDragData& payload() const { return payload_; }

  // Whether the handle is drawn. The grip is always there to be grabbed;
  // this is only whether it shows.
  void SetShown(bool shown);
  bool shown() const { return shown_; }
  bool hovered() const { return hovered_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  ui::Cursor GetCursor(const ui::MouseEvent& event) override;

  // views::DragController:
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override;

 private:
  Delegate delegate_;
  RowDragData payload_;
  // The half the drag image is drawn from.
  SidebarRow image_row_;
  bool shown_ = false;
  bool hovered_ = false;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SPLIT_GRIP_VIEW_H_
