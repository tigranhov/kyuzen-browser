// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/focus_rings.h"

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/view.h"

namespace arcium {

namespace {
constexpr float kThickness = 2.f;
}  // namespace

void StyleFocusRing(views::View* host,
                    float corner_radius,
                    FocusRingPlacement placement) {
  if (!views::FocusRing::Get(host)) {
    views::FocusRing::Install(host);
  }
  views::FocusRing* ring = views::FocusRing::Get(host);
  views::InstallRoundRectHighlightPathGenerator(host, gfx::Insets(),
                                                corner_radius);
  ring->SetColorId(kColorArciumFocusRing);
  ring->SetHaloThickness(kThickness);
  // The halo is centred on the path: half a stroke in keeps it all inside,
  // and half a stroke out plus a hair of air sets it just outside.
  ring->SetHaloInset(placement == FocusRingPlacement::kInside
                         ? kThickness / 2
                         : -kThickness / 2 - 1);
}

}  // namespace arcium
