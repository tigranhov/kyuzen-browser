// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TabListView as a drop target: where a dragged row would land in this list,
// how that boundary is drawn, and what the drop does to the model when it is
// released. Kept apart from the list itself (tab_list_view.cc) because it
// answers a different question -- not "what does this section hold" but
// "where would this land, said in terms the model still understands after a
// nested drag loop has let another window rebuild the list".
//
// One member here is read by the list's own layout: ReservesDropBand(), which
// is why an empty section has a height at all while a drag is running.

#include "arcium/ui/sidebar/tab_list_view.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "arcium/browser/model/reorder_index.h"
#include "arcium/ui/sidebar/folder_header_view.h"
#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"

namespace arcium {

namespace {
// The insertion line. Two device-independent pixels, which is exactly the
// gap BoxLayout leaves between rows, so it sits between them rather than
// over one.
constexpr int kDropLineThickness = 2;
}  // namespace

void TabListView::SetDragSession(RowDragSession* session) {
  drag_session_.Reset();
  if (session) {
    drag_session_.Observe(session);
  }
  OnRowDragInFlightChanged();
}

void TabListView::OnRowDragInFlightChanged() {
  UpdateVisibility();
  PreferredSizeChanged();
}

bool TabListView::ReservesDropBand() const {
  // Only a section with nothing in it needs one: anything with a row already
  // has a boundary to aim between. Today always has the "New tab" row, so
  // this is the Pinned list on a fresh profile — the unreachable half of the
  // central Arc gesture.
  return drag_session_.IsObserving() &&
         drag_session_.GetSource()->in_flight() && rows_.empty() &&
         headers_.empty() && !new_tab_;
}

void TabListView::OnRowDragStarted() {
  if (drag_session_.IsObserving()) {
    drag_session_.GetSource()->Begin(GetWidget());
  }
}

void TabListView::MoveTabBeforeTab(int from_index, int before_tab) {
  // Clamp to this section's range in tab-index space. `rows_` is in laid-out
  // order — a folder's entries come before the top level — so the first and
  // last row are not the smallest and largest tab index, and a cold row has
  // no tab index at all. Taking the ends instead of the extremes hands
  // std::clamp lo > hi, which is a hard abort under libc++ hardening.
  //
  // A two-variable scan rather than a collected std::vector<int>: nothing on
  // a drag path may allocate per event.
  int lo = std::numeric_limits<int>::max();
  int hi = std::numeric_limits<int>::min();
  for (const TabRowView* row : rows_) {
    const int tab_index = row->tab_index();
    if (tab_index >= 0) {
      lo = std::min(lo, tab_index);
      hi = std::max(hi, tab_index);
    }
  }
  // A section of nothing but cold rows has no tab-index range to move within,
  // and a cold row is not being dragged anywhere the tab strip understands.
  if (lo > hi || from_index < 0) {
    return;
  }
  // The anchor names the row the dragged one lands *before*, counted with the
  // dragged row still in the list; a negative anchor is "below everything",
  // which is the end of the range.
  const int to = std::clamp(
      before_tab < 0 ? hi : LiftThenInsertIndex(from_index, before_tab), lo,
      hi);
  if (to != from_index) {
    model_->MoveTab(from_index, to);
  }
}

size_t TabListView::DropRowIndex(int y) const {
  // `rows_` is in laid-out order and BoxLayout lays out by child index, so
  // their y's ascend with the vector even though their tab indices and model
  // positions do not.
  size_t index = 0;
  for (const TabRowView* row : rows_) {
    if (y < row->bounds().CenterPoint().y()) {
      break;
    }
    ++index;
  }
  return index;
}

int TabListView::DropLineY(size_t index) const {
  if (rows_.empty()) {
    return 0;
  }
  if (index < rows_.size()) {
    return std::max(0, rows_[index]->y() - kDropLineThickness);
  }
  return rows_.back()->bounds().bottom();
}

TabListView::DropAnchor TabListView::AnchorForDropIndex(size_t index) const {
  DropAnchor anchor;
  anchor.today_position = static_cast<int>(index);
  if (index < rows_.size()) {
    anchor.before_entry = rows_[index]->row().entry_id;
    anchor.before_tab = rows_[index]->tab_index();
  }
  return anchor;
}

int TabListView::PositionForAnchor(const DropAnchor& anchor) const {
  // No anchor is the end of the section. A collapsed folder's members are
  // real entries that no row was made for, so that end is the count the model
  // gave, not the number of views.
  if (!anchor.before_entry.is_valid()) {
    return section_row_count_;
  }
  for (size_t i = 0; i < rows_.size() && i < row_positions_.size(); ++i) {
    if (rows_[i]->row().entry_id == anchor.before_entry) {
      return row_positions_[i];
    }
  }
  // The row the drop was aimed at is gone — another window unpinned it while
  // the nested loop was running. The geometry the pointer came to rest on is
  // stale with it, so the end of the section is the honest answer rather than
  // a slot that now holds something the user never saw.
  return section_row_count_;
}

std::optional<int> TabListView::EntryPositionInSection(EntryId id) const {
  if (!id.is_valid()) {
    return std::nullopt;
  }
  for (size_t i = 0; i < rows_.size() && i < row_positions_.size(); ++i) {
    if (rows_[i]->row().entry_id == id) {
      return row_positions_[i];
    }
  }
  return std::nullopt;
}

bool TabListView::IsOverHeaderAt(int y) const {
  // A walk over the built headers, with no allocation: asked again on every
  // drag-move event, the same as DropRowIndex.
  for (const std::unique_ptr<FolderHeaderView>& header : headers_) {
    // A header hidden inside a collapsed folder is not in the child list and
    // keeps whatever bounds it last had, so it must not answer for a band it
    // no longer occupies.
    if (header->parent() != this) {
      continue;
    }
    if (y >= header->y() && y < header->bounds().bottom()) {
      return true;
    }
  }
  return false;
}

bool TabListView::DropRefusedByHeader(int y, const RowDragData& payload) const {
  if (!IsOverHeaderAt(y)) {
    return false;
  }
  if (payload.is_folder()) {
    // A header that said no to a folder must go on meaning no one pixel
    // lower. Without this the list behind it takes the drop and the gesture
    // lands somewhere the user was told it could not.
    return true;
  }
  return payload.is_entry() && !CanFolderAcceptEntry(payload.entry_id);
}

void TabListView::SetDropIndex(std::optional<size_t> index) {
  if (drop_index_ == index) {
    return;
  }
  drop_index_ = index;
  SchedulePaint();
}

bool TabListView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  format_types->insert(RowDragData::Format());
  return true;
}

bool TabListView::AreDropTypesRequired() {
  return true;
}

bool TabListView::CanDrop(const ui::OSExchangeData& data) {
  // Favourites are the grid's, not a list's; the other two sections take any
  // row, and which command that becomes is PerformDrop's business.
  //
  // A folder is not a row. Task 9 gives the list's own background a meaning
  // for one -- back to the top level -- and until then a folder dropped
  // anywhere but on a header does nothing, rather than being read as the
  // entry drop it is not.
  std::optional<RowDragData> payload = RowDragData::Read(data);
  return section_ != SidebarSection::kFavorites && payload.has_value() &&
         !payload->is_folder();
}

void TabListView::OnDragEntered(const ui::DropTargetEvent& event) {
  // Once per entry, not once per move: unpickling allocates and a drag-move
  // arrives on every pixel of pointer motion.
  drag_payload_ = RowDragData::Read(event.data());
}

int TabListView::OnDragUpdated(const ui::DropTargetEvent& event) {
  if (!drag_payload_) {
    drag_payload_ = RowDragData::Read(event.data());
  }
  if (!drag_payload_) {
    SetDropIndex(std::nullopt);
    return ui::DragDropTypes::DRAG_NONE;
  }
  const int y = event.location().y();
  if (DropRefusedByHeader(y, *drag_payload_)) {
    // The header under the pointer already said no. No indicator either: a
    // line this list would not honour is the same lie a header's highlight
    // would have told.
    SetDropIndex(std::nullopt);
    return ui::DragDropTypes::DRAG_NONE;
  }
  SetDropIndex(DropRowIndex(y));
  return ui::DragDropTypes::DRAG_MOVE;
}

void TabListView::OnDragExited() {
  drag_payload_.reset();
  SetDropIndex(std::nullopt);
}

views::View::DropCallback TabListView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  std::optional<RowDragData> payload = drag_payload_;
  if (!payload) {
    payload = RowDragData::Read(event.data());
  }
  const int y = event.location().y();
  drag_payload_.reset();
  SetDropIndex(std::nullopt);
  if (!payload || DropRefusedByHeader(y, *payload)) {
    return base::NullCallback();
  }
  const DropAnchor anchor = AnchorForDropIndex(DropRowIndex(y));
  // Weak, and with the payload and the anchor already resolved: the drop runs
  // after the event that produced it, and a model change from another window
  // can rebuild — or destroy — this list in between. The anchor is an entry
  // id rather than a row index because the rows are pooled by index, so a
  // slot survives a rebuild while what it holds does not.
  return base::BindOnce(&TabListView::PerformDrop, weak_factory_.GetWeakPtr(),
                        *payload, anchor);
}

void TabListView::PerformDrop(
    RowDragData payload,
    DropAnchor anchor,
    const ui::DropTargetEvent& event,
    ui::mojom::DragOperation& output_drag_op,
    std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
  output_drag_op = ui::mojom::DragOperation::kMove;
  if (section_ == SidebarSection::kPinned) {
    const int to = PositionForAnchor(anchor);
    if (payload.is_entry()) {
      model_->MoveEntryToSection(payload.entry_id, SidebarSection::kPinned,
                                 ReorderPosition(payload.entry_id, to));
    } else {
      // A Today tab becomes a pinned entry bound to that same tab. The
      // section decides the kind, and the drop index decides the place — the
      // insertion line was drawn there before the gesture was taken.
      model_->MoveTabToSection(payload.tab_index, SidebarSection::kPinned, to);
    }
    return;
  }
  if (payload.is_entry()) {
    // Today holds tabs. The entry goes and its page stays; see
    // SidebarModel::MoveEntryToSection for why that needs no undo. The tab it
    // leaves behind lands where the line was drawn, because Today's order is
    // the tab strip's and this is a strip move like any other.
    model_->MoveEntryToSection(payload.entry_id, SidebarSection::kToday,
                               anchor.today_position);
    return;
  }
  MoveTabBeforeTab(payload.tab_index, anchor.before_tab);
}

int TabListView::ReorderPosition(EntryId id, int to) const {
  // The drop boundary counts this section as it looks now, with the dragged
  // entry still in it, and MoveEntryToSection is lift-then-insert. An entry
  // arriving from another section has no position here, which is exactly the
  // nullopt case.
  return LiftThenInsertIndex(EntryPositionInSection(id), to);
}

void TabListView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (!drop_index_) {
    return;
  }
  canvas->FillRect(
      gfx::Rect(0, DropLineY(*drop_index_), width(), kDropLineThickness),
      GetColorProvider()->GetColor(kColorArciumSpaceAccent));
}

}  // namespace arcium
