// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SPLIT_DROP_VIEW_H_
#define ARCIUM_UI_BROWSER_SPLIT_DROP_VIEW_H_

#include <optional>
#include <set>

#include "arcium/ui/sidebar/row_drag_data.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace arcium {

// The drop target inside SplitBand: a strip beside the page, lit while a
// row is held over it. A row dropped here goes beside the page on screen, on
// the side the band is on.
//
// It accepts Arcium's own row format and no other, so a link dragged from a
// page is left to Chromium's own drop target at the page's edge.
class SplitDropView : public views::View {
  METADATA_HEADER(SplitDropView, views::View)

 public:
  // `right` is which side of the page the dropped row goes on.
  using DropCallback = base::RepeatingCallback<void(RowDragData, bool right)>;

  explicit SplitDropView(DropCallback on_drop);
  SplitDropView(const SplitDropView&) = delete;
  SplitDropView& operator=(const SplitDropView&) = delete;
  ~SplitDropView() override;

  // views::View:
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool AreDropTypesRequired() override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  void PerformDrop(RowDragData payload,
                   bool right,
                   const ui::DropTargetEvent& event,
                   ui::mojom::DragOperation& output_drag_op,
                   std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner);
  void SetLit(bool lit);

  DropCallback on_drop_;
  // Read once per drag rather than per move: the payload cannot change while
  // one drag is in flight.
  std::optional<RowDragData> payload_;
  // True while a row that can be dropped is held over the band.
  bool lit_ = false;

  base::WeakPtrFactory<SplitDropView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SPLIT_DROP_VIEW_H_
