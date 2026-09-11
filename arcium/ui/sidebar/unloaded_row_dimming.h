// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_UNLOADED_ROW_DIMMING_H_
#define ARCIUM_UI_SIDEBAR_UNLOADED_ROW_DIMMING_H_

#include "ui/base/models/image_model.h"

namespace arcium {

// A favicon at reduced opacity, for a row whose click has to load a page
// first -- a closed entry, or a tab whose page is not in memory -- so it reads
// as such beside a loaded one at a glance. Shared by TabRowView and
// FavoritesGridView, the two places a row's favicon is drawn.
//
// Returns an image generator rather than a rasterized bitmap: `favicon` can
// be a vector icon whose colour only resolves once the view is painted with a
// real ColorProvider, exactly the reason ImageView itself defers
// rasterization to paint time. Dimming eagerly here would either rasterize
// with no provider (the wrong colour) or force this helper to take one (which
// a row does not have until it is in a widget -- see SetRowDragImage for the
// same constraint on a different favicon use). `favicon.IsEmpty()` is passed
// through unchanged: there is nothing to dim.
ui::ImageModel DimUnloadedFavicon(const ui::ImageModel& favicon);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_UNLOADED_ROW_DIMMING_H_
