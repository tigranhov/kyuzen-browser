// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/split_band.h"

#include <memory>
#include <utility>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rect.h"

namespace arcium {

SplitBand::SplitBand(BrowserView* browser_view,
                     RowDragSession* session,
                     SplitDropView::DropCallback on_drop)
    : browser_view_(browser_view),
      session_(session),
      on_drop_(std::move(on_drop)) {
  observation_.Observe(session_);
}

SplitBand::~SplitBand() {
  // The window is being torn down, so there is no layout left to ask for.
  if (view_) {
    browser_view_->RemoveChildViewT(view_.ExtractAsDangling());
  }
}

int SplitBand::ReservedWidth() const {
  return view_ ? kWidth : 0;
}

void SplitBand::Layout(const gfx::Rect& page) {
  if (view_) {
    view_->SetBoundsRect(
        gfx::Rect(page.right(), page.y(), kWidth, page.height()));
  }
}

void SplitBand::OnRowDragInFlightChanged() {
  if (session_->in_flight()) {
    Show();
  } else {
    TakeAway();
  }
}

void SplitBand::Show() {
  if (view_) {
    return;
  }
  view_ =
      browser_view_->AddChildView(std::make_unique<SplitDropView>(on_drop_));
  // Above every other layer in the window, for the reason PeekController
  // gives: added last is not enough, because the window restacks its children
  // whenever it lays them out.
  if (ui::Layer* layer = view_->layer(); layer && layer->parent()) {
    layer->parent()->StackAtTop(layer);
  }
  // The page gives up the strip on the next layout, which also places the
  // band in it.
  browser_view_->InvalidateLayout();
}

void SplitBand::TakeAway() {
  if (!view_) {
    return;
  }
  browser_view_->RemoveChildViewT(view_.ExtractAsDangling());
  browser_view_->InvalidateLayout();
}

}  // namespace arcium
