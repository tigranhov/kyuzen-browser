// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/row_drag_session.h"

namespace arcium {

RowDragSession::RowDragSession() = default;

RowDragSession::~RowDragSession() = default;

void RowDragSession::Begin(views::Widget* widget) {
  // A second Begin without an End means the first drag ended somewhere this
  // never heard about; re-pointing the observation is the recovery.
  observation_.Reset();
  if (widget) {
    observation_.Observe(widget);
  }
  SetInFlight(true);
}

void RowDragSession::End() {
  observation_.Reset();
  SetInFlight(false);
}

void RowDragSession::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void RowDragSession::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void RowDragSession::OnWidgetDragDropCompleted(views::Widget* widget) {
  End();
}

void RowDragSession::OnWidgetDestroying(views::Widget* widget) {
  End();
}

void RowDragSession::SetInFlight(bool in_flight) {
  if (in_flight_ == in_flight) {
    return;
  }
  in_flight_ = in_flight;
  observers_.Notify(&Observer::OnRowDragInFlightChanged);
}

}  // namespace arcium
