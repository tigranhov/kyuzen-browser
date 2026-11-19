// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/drop_marks.h"

#include <algorithm>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "cc/paint/paint_flags.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"

namespace arcium {

namespace {
// Thin enough to sit inside a row's padding, thick enough to read as a ring
// rather than a hairline on the accent's own tint.
constexpr float kRingThickness = 1.5f;
}  // namespace

void PaintDropMark(gfx::Canvas* canvas,
                   const gfx::Rect& mark,
                   const ui::ColorProvider* colors) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors->GetColor(kColorArciumSpaceAccent));
  canvas->DrawRoundRect(gfx::RectF(mark),
                        std::min(mark.width(), mark.height()) / 2.f, flags);
}

void PaintDropTarget(gfx::Canvas* canvas,
                     const gfx::RectF& target,
                     float radius,
                     const ui::ColorProvider* colors) {
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(colors->GetColor(kColorArciumSpaceChipActiveBackground));
  canvas->DrawRoundRect(target, radius, fill);

  // The stroke is centred on its path, so the path sits half a stroke in
  // and the ring stays inside the target.
  gfx::RectF ring = target;
  ring.Inset(kRingThickness / 2);
  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeWidth(kRingThickness);
  stroke.setColor(colors->GetColor(kColorArciumSpaceAccent));
  canvas->DrawRoundRect(ring, radius - kRingThickness / 2, stroke);
}

}  // namespace arcium
