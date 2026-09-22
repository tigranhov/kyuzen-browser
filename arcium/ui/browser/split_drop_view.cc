// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/split_drop_view.h"

#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/view_class_properties.h"

namespace arcium {
namespace {

// How much of the page the lit half covers is not a choice -- it is half --
// so the only number here is how strongly it is lit.
constexpr SkAlpha kLitAlpha = 0x38;

}  // namespace

SplitDropView::SplitDropView(DropCallback on_drop)
    : on_drop_(std::move(on_drop)) {
  // The page draws through a compositor layer, and a view without one paints
  // beneath every layer inside its parent's. Stage 4b's peek was invisible
  // for exactly this reason; the caller stacks this layer at the top.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
}

SplitDropView::~SplitDropView() = default;

bool SplitDropView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  format_types->insert(RowDragData::Format());
  return true;
}

bool SplitDropView::AreDropTypesRequired() {
  return true;
}

bool SplitDropView::CanDrop(const ui::OSExchangeData& data) {
  std::optional<RowDragData> payload = RowDragData::Read(data);
  // A folder holds entries rather than a page, so there is nothing a drop of
  // one here could put on screen.
  return payload.has_value() && !payload->is_folder();
}

int SplitDropView::OnDragUpdated(const ui::DropTargetEvent& event) {
  if (!payload_) {
    payload_ = RowDragData::Read(event.data());
  }
  if (!payload_ || payload_->is_folder()) {
    SetLitHalf(std::nullopt);
    return ui::DragDropTypes::DRAG_NONE;
  }
  SetLitHalf(event.location().x() >= width() / 2);
  return ui::DragDropTypes::DRAG_MOVE;
}

void SplitDropView::OnDragExited() {
  payload_.reset();
  SetLitHalf(std::nullopt);
}

views::View::DropCallback SplitDropView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  std::optional<RowDragData> payload = payload_;
  if (!payload) {
    payload = RowDragData::Read(event.data());
  }
  const bool right = event.location().x() >= width() / 2;
  payload_.reset();
  SetLitHalf(std::nullopt);
  if (!payload || payload->is_folder()) {
    return base::NullCallback();
  }
  // Weak, and with the payload resolved already: the drop runs after the
  // event that produced it, and the drag ending destroys this view.
  return base::BindOnce(&SplitDropView::PerformDrop, weak_factory_.GetWeakPtr(),
                        *payload, right);
}

void SplitDropView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (!lit_right_.has_value()) {
    return;
  }
  const int half = width() / 2;
  const gfx::Rect lit = *lit_right_
                            ? gfx::Rect(half, 0, width() - half, height())
                            : gfx::Rect(0, 0, half, height());
  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(SkColorSetA(
      GetColorProvider()->GetColor(kColorArciumRowTextActive), kLitAlpha));
  canvas->DrawRoundRect(lit, metrics::kContentCornerRadius, flags);
}

void SplitDropView::PerformDrop(
    RowDragData payload,
    bool right,
    const ui::DropTargetEvent& event,
    ui::mojom::DragOperation& output_drag_op,
    std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
  output_drag_op = ui::mojom::DragOperation::kMove;
  on_drop_.Run(payload, right);
}

void SplitDropView::SetLitHalf(std::optional<bool> right) {
  if (lit_right_ == right) {
    return;
  }
  lit_right_ = right;
  SchedulePaint();
}

BEGIN_METADATA(SplitDropView)
END_METADATA

}  // namespace arcium
