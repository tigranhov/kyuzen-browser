// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_DROP_MARKS_H_
#define ARCIUM_UI_SIDEBAR_DROP_MARKS_H_

namespace gfx {
class Canvas;
class Rect;
class RectF;
}  // namespace gfx

namespace ui {
class ColorProvider;
}

namespace arcium {

// The two things a drag can promise, drawn the same way everywhere, so one
// meaning has one look.
//
// Where the dragged thing will land among others: an accent line with round
// ends, across a list or upright between two tiles. `mark` is the line's
// whole extent.
void PaintDropMark(gfx::Canvas* canvas,
                   const gfx::Rect& mark,
                   const ui::ColorProvider* colors);

// Something the dragged thing will go into or join -- a folder, a row's
// half, the strip beside the page: the accent's soft tint inside a thin
// accent ring.
void PaintDropTarget(gfx::Canvas* canvas,
                     const gfx::RectF& target,
                     float radius,
                     const ui::ColorProvider* colors);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_DROP_MARKS_H_
