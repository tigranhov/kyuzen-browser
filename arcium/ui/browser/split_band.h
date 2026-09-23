// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SPLIT_BAND_H_
#define ARCIUM_UI_BROWSER_SPLIT_BAND_H_

#include "arcium/ui/browser/split_drop_view.h"
#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/row_drag_session.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"

class BrowserView;

namespace gfx {
class Rect;
}

namespace arcium {

// Where a sidebar row is dropped to put its page beside the one on screen: a
// strip at the page's trailing edge, which the page gives up for as long as a
// row is being dragged and takes back when the drag ends.
//
// Beside the page rather than over it. On macOS the page is a native view
// that takes every drag crossing it before anything the window draws on top
// can see one, so a target laid over the page was never reached. Chromium's
// own target for a link dropped at the page's edge is placed beside the page
// for the same reason.
//
// Nothing is allocated while no row is being dragged: the drop view is built
// when a drag starts and freed when it ends.
class SplitBand : public RowDragSession::Observer {
 public:
  // How much of the page's width the band takes while it is up.
  static constexpr int kWidth = 80;

  // Whether the dragged row may go beside the page on screen. The band is
  // not offered for one that may not: the page on screen itself, a half of
  // the split already up, a folder.
  using CanDropCallback =
      base::RepeatingCallback<bool(const RowDragData& payload)>;

  SplitBand(BrowserView* browser_view,
            RowDragSession* session,
            CanDropCallback can_drop,
            SplitDropView::DropCallback on_drop);
  SplitBand(const SplitBand&) = delete;
  SplitBand& operator=(const SplitBand&) = delete;
  ~SplitBand() override;

  // What the page gives up at its trailing edge: kWidth while a row is being
  // dragged, nothing otherwise. Read by the window's layout.
  int ReservedWidth() const;

  // Puts the band in the strip beside `page`, both panes of it, in the
  // window's coordinates. Called after every layout of the window.
  void Layout(const gfx::Rect& page);

  const SplitDropView* view_for_testing() const { return view_; }

  // RowDragSession::Observer:
  void OnRowDragInFlightChanged() override;

 private:
  void Show();
  void TakeAway();

  raw_ptr<BrowserView> browser_view_;
  raw_ptr<RowDragSession> session_;
  CanDropCallback can_drop_;
  SplitDropView::DropCallback on_drop_;
  // A child of the window, owned by it, and alive only during a drag.
  raw_ptr<SplitDropView> view_ = nullptr;
  base::ScopedObservation<RowDragSession, RowDragSession::Observer>
      observation_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SPLIT_BAND_H_
