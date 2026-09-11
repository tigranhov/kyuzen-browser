// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/unloaded_row_dimming.h"

#include "base/functional/bind.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"

namespace arcium {

namespace {

// Low enough to read as "this has to load" next to a loaded row's
// full-strength favicon, high enough that the icon -- often the only way to
// tell one dimmed row from another -- is still legible.
constexpr double kUnloadedFaviconAlpha = 0.45;

gfx::ImageSkia Dim(ui::ImageModel favicon, const ui::ColorProvider* provider) {
  return gfx::ImageSkiaOperations::CreateTransparentImage(
      favicon.Rasterize(provider), kUnloadedFaviconAlpha);
}

}  // namespace

ui::ImageModel DimUnloadedFavicon(const ui::ImageModel& favicon) {
  if (favicon.IsEmpty()) {
    return favicon;
  }
  return ui::ImageModel::FromImageGenerator(base::BindRepeating(&Dim, favicon),
                                            favicon.Size());
}

}  // namespace arcium
