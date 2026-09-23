// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_
#define ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/row_drag_session.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"

namespace ui {
class ClipboardFormatType;
class OSExchangeData;
}  // namespace ui

namespace views {
class LabelButton;
}

namespace arcium {

class FolderHeaderView;
class RowContextMenu;
class TabRowView;

// A vertical list for one section: folder headers, each followed by its
// entries, then the entries in no folder. Only the Pinned section has
// folders. The Today list also shows the "New tab" row at its end.
class TabListView : public views::View, public RowDragSession::Observer {
  METADATA_HEADER(TabListView, views::View)

 public:
  TabListView(SidebarModel* model, SidebarSection section);

  // Which section this list draws. Lets a caller tell the Pinned list from
  // the Today one without depending on their order among their siblings.
  SidebarSection section() const { return section_; }
  TabListView(const TabListView&) = delete;
  TabListView& operator=(const TabListView&) = delete;
  ~TabListView() override;

  // views::View:
  void Layout(PassKey) override;

  // Filters `rows` to this section and updates children, reusing views.
  void SetRows(const std::vector<SidebarRow>& rows);

  // The shared "a sidebar row is being dragged" signal. This list is both a
  // source — its rows announce their drags — and, when it is empty, a target
  // that only exists while one is running. Null detaches, which is what
  // SidebarView does before the session it owns goes away.
  void SetDragSession(RowDragSession* session);

  size_t row_count() const { return rows_.size(); }
  size_t folder_count() const { return headers_.size(); }
  // Where the insertion line is being drawn, or nothing when no drag is over
  // this list. An index into the laid-out rows; row_count() means "after the
  // last one".
  std::optional<size_t> drop_index_for_testing() const { return drop_index_; }
  // The top of the insertion line in this list's coordinates, or -1 when no
  // line is drawn.
  int drop_line_y_for_testing() const {
    return drop_index_ ? DropLineY(*drop_index_) : -1;
  }

  // views::View:
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool AreDropTypesRequired() override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  void OnDragEntered(const ui::DropTargetEvent& event) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override;
  void OnPaint(gfx::Canvas* canvas) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // RowDragSession::Observer:
  void OnRowDragInFlightChanged() override;

 private:
  // What a drop is aimed at, said in the model's terms rather than in the
  // view's. A drag runs a nested loop and another window on the same profile
  // can rebuild this list inside it, so an index names a slot that may hold
  // something else by the time the drop runs. Resolved once, in
  // GetDropCallback, off the rows the pointer was actually over.
  struct DropAnchor {
    // Entry sections: the entry the dragged row lands before. Invalid means
    // the end of the section, which is also what an anchor the model has
    // since dropped falls back to.
    EntryId before_entry;
    // Today: the tab it lands before, or -1 for the end. Today's order is the
    // tab strip's, so a strip index is the stable name for a place in it.
    int before_tab = -1;
    // Today: the same boundary counted in rows, which is the position an
    // entry dropped into Today asks for its tab to be put at.
    int today_position = 0;
  };

  // Where one child of this list sits in the laid-out order.
  struct PlanItem {
    bool is_header = false;
    // Index into the folder list for a header, into the section's rows for a
    // row. Both are positions in the vectors SetRows built.
    size_t index = 0;
    // How far in it is drawn, in folder levels: a header sits at its folder's
    // depth and its rows one level further in.
    int depth = 0;
  };

  // A row backed by an entry commands the entry; a Today row commands the
  // tab. Only the first can be cold, and a cold row has no tab index.
  void OnActivateRow(const SidebarRow& row);
  void OnCloseRow(const SidebarRow& row);
  void OnRenameRow(const SidebarRow& row, const std::u16string& title);
  void OnRevertRow(const SidebarRow& row);
  void OnShowRowMenu(TabRowView* source,
                     const SidebarRow& row,
                     const gfx::Point& point);
  void OnToggleFolder(const SidebarFolder& folder);
  void OnRenameFolder(FolderId id, const std::u16string& name);
  void OnShowFolderMenu(FolderHeaderView* source,
                        const SidebarFolder& folder,
                        const gfx::Point& point);
  // A row dropped on one of this list's folder headers.
  void OnDropOnFolder(EntryId id, const SidebarFolder& folder);
  // Whether one of this list's folders could hold `id` at all. Folders hold
  // pinned entries only — Task 7's rule — so a favourite dropped on a header
  // has to be refused rather than accepted and silently discarded by
  // MoveEntryToFolder. The header asks its owner because the owner is the
  // one that knows which section it draws and which entries it holds.
  bool CanFolderAcceptEntry(EntryId id) const;
  // Whether one of this list's folders could hold the folder `id`. The list
  // asks the model rather than reading its own rows: a cycle and the depth
  // cap are properties of the whole tree, and the model is its authority.
  bool CanFolderAcceptFolder(FolderId id, FolderId parent) const;
  // A folder header dropped on one of this list's folder headers.
  void OnDropFolderOnFolder(FolderId id, const SidebarFolder& folder);
  // One of this list's rows started a drag.
  void OnRowDragStarted();

  // Where in the laid-out rows a drop at `y` would insert: 0..rows_.size().
  size_t DropRowIndex(int y) const;
  // The y of the boundary the insertion line is drawn on for `index`.
  int DropLineY(size_t index) const;
  // The row at `index`, named by what the model calls it.
  DropAnchor AnchorForDropIndex(size_t index) const;
  // The anchor turned into a position among this section's entries. `rows_`
  // is in laid-out order — a folder's members come before the top level —
  // which is not the order the model keeps positions in, so this reads the
  // position the anchoring row carries rather than assuming a slot is one.
  int PositionForAnchor(const DropAnchor& anchor) const;
  // The position `id` holds among this section's entries right now, or
  // nothing when this section does not hold it.
  std::optional<int> EntryPositionInSection(EntryId id) const;
  // Whether one of this list's folder headers sits under `y`, in this list's
  // own coordinates — the same space `event.location()` arrives in once
  // DropHelper walks a refused header's drop up to its owning list.
  bool IsOverHeaderAt(int y) const;
  // Whether the drop at `y` must be refused because the header under the
  // pointer refused it. Only an entry can be refused this way: a Today tab is
  // never `is_entry()`, so it always falls through and lands pinned at the
  // top level, which invents no kind change for anyone to silently suffer. An
  // entry a header would refuse — a favourite, or one the model has since
  // dropped — must not quietly land here instead, one section down from where
  // the header said no. Routes through CanFolderAcceptEntry, the exact
  // predicate every header in this list already asks, rather than
  // re-deriving the answer: it is also the one that keeps working when the
  // model drops the id mid-drag.
  bool DropRefusedByHeader(int y, const RowDragData& payload) const;
  // The drop boundary `to` turned into the position ReorderEntry wants, which
  // differ by one whenever the entry is moving down inside its own section.
  int ReorderPosition(EntryId id, int to) const;
  // Today only: turns the anchor into a tab-strip move.
  void MoveTabBeforeTab(int from_index, int before_tab);
  void SetDropIndex(std::optional<size_t> index);
  // Bound at drop time with the payload and the anchor already resolved,
  // because the drop runs after the event that produced it.
  void PerformDrop(RowDragData payload,
                   DropAnchor anchor,
                   const ui::DropTargetEvent& event,
                   ui::mojom::DragOperation& output_drag_op,
                   std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner);
  // An empty section has nothing to hit, so while a row drag is running it
  // reserves a band to aim at. Only while one is running: idle layout, and
  // Stage 1's snapshot baselines with it, must not move.
  bool ReservesDropBand() const;
  void UpdateVisibility();

  TabRowView* MakeRow();
  std::unique_ptr<FolderHeaderView> MakeHeader();

  raw_ptr<SidebarModel> model_;
  const SidebarSection section_;
  std::vector<raw_ptr<TabRowView>> rows_;
  // Parallel to `rows_`: each row's position among this section's rows, which
  // for an entry section is the position the model orders by. Kept because
  // `rows_` is in laid-out order and that order is not the model's. A row
  // drawn here from another section -- a Today tab beside the pinned entry
  // it shares the screen with -- has no position of its own and carries the
  // one the next row of this section would have.
  std::vector<int> row_positions_;
  // How many rows this section has, including the members of collapsed
  // folders that were never built. The end of the section, for a drop past
  // the last visible row.
  int section_row_count_ = 0;
  // One header per folder the model handed over, hidden ones included. Owned
  // here rather than by the child list, because a header inside a collapsed
  // folder is taken out of the child list — a subtree nobody can see is not
  // laid out and not painted — and a view with no parent has no other owner.
  // It is kept rather than destroyed so reopening the folder allocates
  // nothing.
  std::vector<std::unique_ptr<FolderHeaderView>> headers_;
  raw_ptr<views::LabelButton> new_tab_ = nullptr;
  // Outlives the menu it is running, so a command that arrives after the
  // click still finds its model and its snapshot.
  std::unique_ptr<RowContextMenu> context_menu_;
  // Read once when a drag enters and reused for every move over the list:
  // reading it back out of the pickle costs an allocation, and a drag-move
  // arrives on every pixel of pointer motion.
  std::optional<RowDragData> drag_payload_;
  std::optional<size_t> drop_index_;
  base::ScopedObservation<RowDragSession, RowDragSession::Observer>
      drag_session_{this};
  base::WeakPtrFactory<TabListView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_
