// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_ROW_DRAG_SESSION_H_
#define ARCIUM_UI_SIDEBAR_ROW_DRAG_SESSION_H_

#include <optional>

#include "arcium/ui/sidebar/row_drag_data.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_observation.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace arcium {

// Whether one of the sidebar's own rows is being dragged right now, shared by
// the sections that need to know.
//
// An empty section is a section with nothing to hit: the grid hides itself
// when it holds no tiles and a list hides itself when it holds no rows, and a
// hidden view is skipped by GetEventHandlerForPoint while a visible one with
// empty bounds has nothing to land on. On a fresh profile that makes both
// "Today tab -> Favourites" and "Today tab -> Pinned" — pinning, the central
// Arc gesture — unreachable by drag. So an empty section reserves a drop band,
// but only while a row drag is in flight, which leaves idle layout and Stage
// 1's snapshot baselines exactly as they were.
//
// Views has no ambient "a drag is running" signal. Both of the sidebar's drag
// sources are Arcium views, so the start is theirs to announce; the end comes
// from the widget, which reports it whether or not the row that started the
// drag survived the rebuild the drop caused — views::View::OnDragDone is
// skipped for a source view that was destroyed inside the nested loop, and
// this list rebuilds inside it routinely.
//
// Once per drag, never per drag-move: nothing on this path runs while the
// pointer is moving.
class RowDragSession : public views::WidgetObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnRowDragInFlightChanged() = 0;
  };

  RowDragSession();
  RowDragSession(const RowDragSession&) = delete;
  RowDragSession& operator=(const RowDragSession&) = delete;
  ~RowDragSession() override;

  bool in_flight() const { return in_flight_; }
  // What the running drag carries, so a target outside the sidebar can decide
  // before the pointer reaches it whether to offer itself at all. Empty when
  // no drag is running.
  const std::optional<RowDragData>& payload() const { return payload_; }

  // Called by a drag source as it writes its payload. `widget` is the source's
  // own widget and is what ends the session; a null one leaves End() to the
  // caller, which is how a unit test with no nested loop to run drives it.
  void Begin(views::Widget* widget, const RowDragData& payload);
  void End();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // views::WidgetObserver:
  void OnWidgetDragDropCompleted(views::Widget* widget) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void SetInFlight(bool in_flight);

  bool in_flight_ = false;
  std::optional<RowDragData> payload_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
  base::ObserverList<Observer> observers_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_ROW_DRAG_SESSION_H_
