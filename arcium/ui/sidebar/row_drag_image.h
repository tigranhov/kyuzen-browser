// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_ROW_DRAG_IMAGE_H_
#define ARCIUM_UI_SIDEBAR_ROW_DRAG_IMAGE_H_

namespace gfx {
class Point;
}

namespace ui {
class OSExchangeData;
}

namespace views {
class View;
}

namespace arcium {

struct SidebarRow;

// Gives `data` the picture that follows the pointer while a row is dragged.
//
// Not cosmetic, and not optional. DragDropClientMac turns whatever the
// provider holds into an NSImage and DCHECKs that it is not zero-sized, so a
// drag started without one aborts the browser on macOS; with DCHECKs off it
// drags an invisible image instead. Every WriteDragDataForView in the sidebar
// calls this, and a new drag source that forgets to is a crash, not a
// blemish.
//
// `row` must name something: a row with neither a title nor a URL draws a
// button with no accessible name, which fails Views' accessibility paint
// check and aborts the same builds a zero-size image does. Nothing reaches
// here with one, because GetDragOperationsForView refuses a row that names
// neither an entry nor a tab before a drag can start.
//
// Built from the row's URL, title and favicon rather than from the dragged
// view's own pixels. A row is drawn before its favicon or its title has
// arrived, and painting a live view would mean moving it to the origin and
// back mid-drag; the shared Views helper substitutes a default favicon and
// the URL text, so the result is never empty whatever the row is missing.
void SetRowDragImage(const SidebarRow& row,
                     views::View* source,
                     const gfx::Point& press_pt,
                     ui::OSExchangeData* data);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_ROW_DRAG_IMAGE_H_
