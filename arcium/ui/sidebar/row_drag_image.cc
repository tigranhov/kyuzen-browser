// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/row_drag_image.h"

#include "arcium/ui/sidebar/sidebar_model.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/button_drag_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {

void SetRowDragImage(const SidebarRow& row,
                     views::View* source,
                     const gfx::Point& press_pt,
                     ui::OSExchangeData* data) {
  gfx::ImageSkia icon;
  // Rasterizing wants a colour provider, which a view only has once it is in
  // a widget. Leaving the icon empty is a fallback rather than a failure: the
  // helper draws the default favicon for it.
  if (!row.favicon.IsEmpty() && source && source->GetWidget()) {
    icon = row.favicon.Rasterize(source->GetColorProvider());
  }
  // The image only. The payload is Arcium's own clipboard format, written by
  // the caller; a sidebar drag is not a URL drop into another application and
  // must not advertise itself as one.
  button_drag_utils::SetDragImage(row.url, row.title, icon, &press_pt, data);
}

}  // namespace arcium
