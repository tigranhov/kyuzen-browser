// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tint_background.h"

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/space_gradients.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace arcium {

TintBackground::TintBackground() = default;
TintBackground::~TintBackground() = default;

void TintBackground::SetPreset(int preset) {
  const bool known =
      preset >= 0 && static_cast<size_t>(preset) < SpaceGradients().size();
  const int chosen = known ? preset : 0;
  if (chosen == preset_) {
    return;
  }
  preset_ = chosen;
  shader_ = nullptr;
}

TintBackground::Stops TintBackground::StopsFor(const views::View& view) const {
  const ui::ColorProvider* cp = view.GetColorProvider();
  const SkColor mixer_bottom =
      cp->GetColor(kColorArciumSidebarBackgroundBottom);
  if (preset_ == 0) {
    return {cp->GetColor(kColorArciumSidebarBackgroundTop), mixer_bottom};
  }
  // Light or dark as the colour mixer chose, read off the colour it chose for
  // this same surface. The mode reaches the mixer by routes the view cannot
  // see -- off the record is always dark, and the browser's own appearance
  // setting overrides the OS -- and the row text was mixed for that mode, so
  // a preset painted in the other one would put dark text on a dark tint.
  const SpaceGradient& gradient = SpaceGradients()[preset_];
  return color_utils::IsDark(mixer_bottom)
             ? Stops{gradient.dark_top, gradient.dark_bottom}
             : Stops{gradient.light_top, gradient.light_bottom};
}

void TintBackground::Paint(gfx::Canvas* canvas, views::View* view) const {
  const Stops stops = StopsFor(*view);
  const gfx::Size size = view->size();
  if (!shader_ || size != cached_size_ || stops.top != cached_top_ ||
      stops.bottom != cached_bottom_) {
    const SkPoint points[2] = {
        SkPoint::Make(0, 0),
        SkPoint::Make(size.width() * 0.4f, size.height() * 0.55f)};
    const SkColor4f colors[2] = {SkColor4f::FromColor(stops.top),
                                 SkColor4f::FromColor(stops.bottom)};
    shader_ = cc::PaintShader::MakeLinearGradient(points, colors, nullptr, 2,
                                                  SkTileMode::kClamp);
    cached_size_ = size;
    cached_top_ = stops.top;
    cached_bottom_ = stops.bottom;
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
