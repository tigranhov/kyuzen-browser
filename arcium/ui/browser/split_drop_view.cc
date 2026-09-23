// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/split_drop_view.h"

#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/view_class_properties.h"

namespace arcium {
namespace {

// The band is drawn for the whole drag, so a row has somewhere visible to
// go, and drawn stronger while one is held over it.
constexpr SkAlpha kRestingAlpha = 0x18;
constexpr SkAlpha kLitAlpha = 0x40;
// Clear of the page on one side and the window's edge on the other.
constexpr int kInset = 6;
constexpr int kIconSize = 20;

}  // namespace

SplitDropView::SplitDropView(DropCallback on_drop)
    : on_drop_(std::move(on_drop)) {
  // The page draws through a compositor layer, and a view without one paints
  // beneath every layer inside its parent's. The band sits beside the page
  // rather than over it, but the window's other layers still overlap its
  // strip; the caller stacks this layer at the top.
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
    SetLit(false);
    return ui::DragDropTypes::DRAG_NONE;
  }
  SetLit(true);
  return ui::DragDropTypes::DRAG_MOVE;
}

void SplitDropView::OnDragExited() {
  payload_.reset();
  SetLit(false);
}

views::View::DropCallback SplitDropView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  std::optional<RowDragData> payload = payload_;
  if (!payload) {
    payload = RowDragData::Read(event.data());
  }
  payload_.reset();
  SetLit(false);
  if (!payload || payload->is_folder()) {
    return base::NullCallback();
  }
  // Weak, and with the payload resolved already: the drop runs after the
  // event that produced it, and the drag ending destroys this view.
  // The band is at the page's trailing edge, so that is the side the page
  // goes on.
  return base::BindOnce(&SplitDropView::PerformDrop, weak_factory_.GetWeakPtr(),
                        *payload, /*right=*/true);
}

void SplitDropView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  const SkColor color = GetColorProvider()->GetColor(kColorArciumRowTextActive);
  gfx::Rect band = GetLocalBounds();
  band.Inset(gfx::Insets::VH(0, kInset));
  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setAntiAlias(true);
  flags.setColor(SkColorSetA(color, lit_ ? kLitAlpha : kRestingAlpha));
  canvas->DrawRoundRect(band, metrics::kContentCornerRadius, flags);
  // The same two panes the sidebar marks a split row with, so the band says
  // what dropping on it does.
  const gfx::ImageSkia icon =
      gfx::CreateVectorIcon(kSplitIcon, kIconSize, color);
  canvas->DrawImageInt(icon, band.CenterPoint().x() - kIconSize / 2,
                       band.CenterPoint().y() - kIconSize / 2);
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

void SplitDropView::SetLit(bool lit) {
  if (lit_ == lit) {
    return;
  }
  lit_ = lit;
  SchedulePaint();
}

BEGIN_METADATA(SplitDropView)
END_METADATA

}  // namespace arcium
