// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_FOCUS_RINGS_H_
#define ARCIUM_UI_SIDEBAR_FOCUS_RINGS_H_

namespace views {
class View;
}

namespace arcium {

// Keyboard focus has one look: a 2pt ring in the accent, following the
// view's rounded shape. A row, tile or chip draws it on its inside edge,
// where it cannot touch the row next to it; a button, the pill and a field
// draw it just outside, where it frames the control without covering it.
// The ring is the host's own focus ring, so nothing is drawn until the view
// has focus.
enum class FocusRingPlacement { kInside, kOutside };

void StyleFocusRing(views::View* host,
                    float corner_radius,
                    FocusRingPlacement placement);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_FOCUS_RINGS_H_
