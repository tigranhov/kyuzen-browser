// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/split_grip_view.h"

#include <algorithm>
#include <utility>

#include "arcium/ui/sidebar/row_drag_image.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "base/numerics/safe_conversions.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"

namespace arcium {

namespace {

// Two columns of three dots, the usual mark for "grab here".
constexpr float kDotRadius = 1.25f;
constexpr int kDotStep = 4;
constexpr base::TimeDelta kOpenDuration = base::Milliseconds(150);

}  // namespace

SplitGripView::SplitGripView(Delegate delegate)
    : delegate_(std::move(delegate)) {
  set_drag_controller(this);
}

SplitGripView::~SplitGripView() = default;

void SplitGripView::SetPair(const SidebarRow& first, const SidebarRow& second) {
  const SidebarRow& named =
      !first.entry_id.is_valid() && second.entry_id.is_valid() ? second : first;
  payload_ = RowDragData();
  payload_.entry_id = named.entry_id;
  payload_.tab_index = named.tab_index;
  payload_.split_pair = true;
  image_row_ = named;
}

void SplitGripView::SetShown(bool shown) {
  if (shown_ == shown) {
    return;
  }
  shown_ = shown;
  // Reduced motion gets the end state at once, which a zero length does.
  open_.SetSlideDuration(gfx::Animation::ShouldRenderRichAnimation()
                             ? kOpenDuration
                             : base::TimeDelta());
  shown ? open_.Show() : open_.Hide();
}

void SplitGripView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  // The dots come in over the second half of the opening, once there is room
  // for them, and go first on the way back.
  const double alpha = std::clamp(openness() * 2 - 1, 0.0, 1.0);
  if (alpha <= 0) {
    return;
  }
  const SkColor color =
      GetColorProvider()->GetColor(kColorArciumRowTextSecondary);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(
      SkColorSetA(color, base::ClampRound<U8CPU>(SkColorGetA(color) * alpha)));
  const gfx::PointF center(width() / 2.0f, height() / 2.0f);
  for (int column : {-1, 1}) {
    for (int row : {-1, 0, 1}) {
      canvas->DrawCircle(gfx::PointF(center.x() + column * kDotStep / 2.0f,
                                     center.y() + row * kDotStep),
                         kDotRadius, flags);
    }
  }
}

void SplitGripView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  if (delegate_.hover_changed) {
    delegate_.hover_changed.Run();
  }
}

void SplitGripView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  if (delegate_.hover_changed) {
    delegate_.hover_changed.Run();
  }
}

ui::Cursor SplitGripView::GetCursor(const ui::MouseEvent& event) {
  return ui::Cursor(ui::mojom::CursorType::kGrab);
}

void SplitGripView::WriteDragDataForView(views::View* sender,
                                         const gfx::Point& press_pt,
                                         ui::OSExchangeData* data) {
  payload_.Write(data);
  SetRowDragImage(image_row_, sender, press_pt, data);
  // Once per drag, at the one moment a source knows one is starting.
  if (delegate_.drag_started) {
    delegate_.drag_started.Run(payload_);
  }
}

int SplitGripView::GetDragOperationsForView(views::View* sender,
                                            const gfx::Point& p) {
  return payload_.is_entry() || payload_.is_tab()
             ? ui::DragDropTypes::DRAG_MOVE
             : ui::DragDropTypes::DRAG_NONE;
}

bool SplitGripView::CanStartDragForView(views::View* sender,
                                        const gfx::Point& press_pt,
                                        const gfx::Point& p) {
  return true;
}

void SplitGripView::AnimationProgressed(const gfx::Animation* animation) {
  SchedulePaint();
  if (delegate_.open_changed) {
    delegate_.open_changed.Run();
  }
}

void SplitGripView::AnimationEnded(const gfx::Animation* animation) {
  AnimationProgressed(animation);
}

BEGIN_METADATA(SplitGripView)
END_METADATA

}  // namespace arcium
