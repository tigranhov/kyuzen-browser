// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tint_background.h"

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace arcium {

TintBackground::TintBackground() = default;
TintBackground::~TintBackground() = default;

void TintBackground::Paint(gfx::Canvas* canvas, views::View* view) const {
  const ui::ColorProvider* cp = view->GetColorProvider();
  const SkColor top = cp->GetColor(kColorArciumSidebarBackgroundTop);
  const SkColor bottom = cp->GetColor(kColorArciumSidebarBackgroundBottom);
  const gfx::Size size = view->size();
  if (!shader_ || size != cached_size_ || top != cached_top_ ||
      bottom != cached_bottom_) {
    const SkPoint points[2] = {
        SkPoint::Make(0, 0),
        SkPoint::Make(size.width() * 0.4f, size.height() * 0.55f)};
    const SkColor4f colors[2] = {SkColor4f::FromColor(top),
                                 SkColor4f::FromColor(bottom)};
    shader_ = cc::PaintShader::MakeLinearGradient(points, colors, nullptr, 2,
                                                  SkTileMode::kClamp);
    cached_size_ = size;
    cached_top_ = top;
    cached_bottom_ = bottom;
  }
  cc::PaintFlags flags;
  flags.setShader(shader_);
  canvas->DrawRect(gfx::Rect(size), flags);
}

void TintBackground::OnViewThemeChanged(views::View* view) {
  shader_ = nullptr;
  view->SchedulePaint();
}

}  // namespace arcium
