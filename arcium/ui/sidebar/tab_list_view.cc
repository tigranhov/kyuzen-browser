// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tab_list_view.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "arcium/ui/sidebar/folder_header_view.h"
#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

namespace {
// The insertion line. Two device-independent pixels, which is exactly the
// gap BoxLayout leaves between rows, so it sits between them rather than
// over one.
constexpr int kDropLineThickness = 2;
}  // namespace

TabListView::TabListView(SidebarModel* model, SidebarSection section)
    : model_(model), section_(section) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  if (section_ == SidebarSection::kToday) {
    new_tab_ = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SidebarModel::NewTab, base::Unretained(model_)),
        u"New tab"));
    new_tab_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(kAddIcon, kColorArciumRowTextSecondary,
                                       metrics::kFaviconSize));
    new_tab_->SetEnabledTextColors(kColorArciumRowTextSecondary);
    new_tab_->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(0, metrics::kRowHorizontalPadding)));
    new_tab_->SetMinSize(gfx::Size(0, metrics::kRowHeight));
    new_tab_->SetImageLabelSpacing(metrics::kRowIconTextGap);
    new_tab_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // See the note in TabRowView: the Today list is inside a ScrollView.
    new_tab_->SetTextSubpixelRenderingEnabled(false);
  }
}

TabListView::~TabListView() = default;

TabRowView* TabListView::MakeRow() {
  TabRowView::Delegate delegate;
  delegate.activate =
      base::BindRepeating(&TabListView::OnActivateRow, base::Unretained(this));
  delegate.close =
      base::BindRepeating(&TabListView::OnCloseRow, base::Unretained(this));
  delegate.rename =
      base::BindRepeating(&TabListView::OnRenameRow, base::Unretained(this));
  delegate.return_to_pinned_url =
      base::BindRepeating(&TabListView::OnRevertRow, base::Unretained(this));
  delegate.show_context_menu =
      base::BindRepeating(&TabListView::OnShowRowMenu, base::Unretained(this));
  delegate.drag_started = base::BindRepeating(&TabListView::OnRowDragStarted,
                                              base::Unretained(this));
  return AddChildView(std::make_unique<TabRowView>(std::move(delegate)));
}

FolderHeaderView* TabListView::MakeHeader() {
  FolderHeaderView::Delegate delegate;
  delegate.toggle_collapsed =
      base::BindRepeating(&TabListView::OnToggleFolder, base::Unretained(this));
  delegate.rename =
      base::BindRepeating(&TabListView::OnRenameFolder, base::Unretained(this));
  delegate.show_context_menu = base::BindRepeating(
      &TabListView::OnShowFolderMenu, base::Unretained(this));
  delegate.drop_entry =
      base::BindRepeating(&TabListView::OnDropOnFolder, base::Unretained(this));
  delegate.can_accept_entry = base::BindRepeating(
      &TabListView::CanFolderAcceptEntry, base::Unretained(this));
  return AddChildView(std::make_unique<FolderHeaderView>(std::move(delegate)));
}

void TabListView::SetRows(const std::vector<SidebarRow>& all_rows) {
  std::vector<const SidebarRow*> mine;
  for (const SidebarRow& row : all_rows) {
    if (row.section == section_) {
      mine.push_back(&row);
    }
  }
  // Only pinned entries live in folders, so no other section asks for them.
  std::vector<SidebarFolder> folders;
  if (section_ == SidebarSection::kPinned) {
    folders = model_->folders();
  }
  std::set<FolderId> known;
  for (const SidebarFolder& folder : folders) {
    known.insert(folder.id);
  }

  // The laid-out order: each folder's header, then its entries when it is
  // expanded, then everything at the top level. A collapsed folder
  // contributes only its header.
  std::vector<PlanItem> plan;
  plan.reserve(mine.size() + folders.size());
  for (size_t f = 0; f < folders.size(); ++f) {
    plan.push_back({/*is_header=*/true, f, /*indented=*/false});
    if (folders[f].collapsed) {
      continue;
    }
    for (size_t r = 0; r < mine.size(); ++r) {
      if (mine[r]->folder_id == folders[f].id) {
        plan.push_back({/*is_header=*/false, r, /*indented=*/true});
      }
    }
  }
  for (size_t r = 0; r < mine.size(); ++r) {
    // A row naming a folder this section did not get back is drawn at the top
    // level rather than lost: the model is the authority on which folders
    // exist, and a stale id must not hide a tab.
    if (!mine[r]->folder_id.has_value() || !known.count(*mine[r]->folder_id)) {
      plan.push_back({/*is_header=*/false, r, /*indented=*/false});
    }
  }

  size_t rows_needed = 0;
  for (const PlanItem& item : plan) {
    rows_needed += item.is_header ? 0u : 1u;
  }

  // Grow or shrink the pools, then assign in order. Views are reused by
  // position so a title change or reorder does not allocate.
  while (headers_.size() < folders.size()) {
    headers_.push_back(MakeHeader());
  }
  while (headers_.size() > folders.size()) {
    FolderHeaderView* header = headers_.back();
    headers_.pop_back();
    RemoveChildViewT(header);
  }
  while (rows_.size() < rows_needed) {
    rows_.push_back(MakeRow());
  }
  while (rows_.size() > rows_needed) {
    TabRowView* row = rows_.back();
    rows_.pop_back();
    RemoveChildViewT(row);
  }

  for (size_t f = 0; f < folders.size(); ++f) {
    headers_[f]->SetFolder(folders[f]);
  }
  // One walk assigns the data and puts the children in the plan's order;
  // BoxLayout lays them out by child index.
  size_t next_row = 0;
  size_t child_index = 0;
  row_positions_.clear();
  row_positions_.reserve(rows_needed);
  section_row_count_ = static_cast<int>(mine.size());
  for (const PlanItem& item : plan) {
    views::View* view = nullptr;
    if (item.is_header) {
      view = headers_[item.index];
    } else {
      TabRowView* row = rows_[next_row++];
      // `mine` is in the order the model hands the section over, which for an
      // entry section is position order. The plan is not, so the position
      // travels with the row rather than being read off its slot later.
      row_positions_.push_back(static_cast<int>(item.index));
      row->SetRow(*mine[item.index]);
      row->SetProperty(
          views::kMarginsKey,
          gfx::Insets::TLBR(0, item.indented ? metrics::kFolderIndent : 0, 0,
                            0));
      view = row;
    }
    ReorderChildView(view, child_index++);
  }

  UpdateVisibility();
  InvalidateLayout();
}

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

void TabListView::UpdateVisibility() {
  SetVisible(!rows_.empty() || !headers_.empty() || new_tab_ ||
             ReservesDropBand());
}

gfx::Size TabListView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = views::View::CalculatePreferredSize(available_size);
  if (ReservesDropBand()) {
    size.SetToMax(
        gfx::Size(available_size.width().value_or(metrics::kSidebarWidth),
                  metrics::kRowHeight));
  }
  return size;
}

void TabListView::OnRowDragStarted() {
  if (drag_session_.IsObserving()) {
    drag_session_.GetSource()->Begin(GetWidget());
  }
}

void TabListView::OnActivateRow(const SidebarRow& row) {
  if (row.entry_id.is_valid()) {
    model_->ActivateEntry(row.entry_id);
  } else {
    model_->ActivateTab(row.tab_index);
  }
}

void TabListView::OnCloseRow(const SidebarRow& row) {
  // Closing a warm entry's tab leaves the entry behind, cold.
  if (row.entry_id.is_valid()) {
    model_->CloseEntryTab(row.entry_id);
  } else {
    model_->CloseTab(row.tab_index);
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
  int to = hi;
  if (before_tab >= 0) {
    to = before_tab;
    // The anchor names the row the dragged one lands *before*. Lifting the
    // dragged row out first shifts everything after it up one, so landing
    // before a row that is already below it means one index less.
    if (from_index < to) {
      --to;
    }
  }
  to = std::clamp(to, lo, hi);
  if (to != from_index) {
    model_->MoveTab(from_index, to);
  }
}

void TabListView::OnRenameRow(EntryId id, const std::u16string& title) {
  if (id.is_valid()) {
    model_->SetEntryTitle(id, title);
  }
}

void TabListView::OnRevertRow(const SidebarRow& row) {
  if (row.entry_id.is_valid()) {
    model_->ReturnToPinnedUrl(row.entry_id);
  }
}

void TabListView::OnShowRowMenu(TabRowView* source,
                                const SidebarRow& row,
                                const gfx::Point& point) {
  context_menu_ = std::make_unique<RowContextMenu>(model_);
  // Weak: a command can rebuild the list before the menu's item runs, and the
  // row the menu was opened from may be gone by then.
  context_menu_->RunForRow(
      row, source, point,
      base::BindRepeating(&TabRowView::BeginRename, source->GetWeakPtr()));
}

void TabListView::OnToggleFolder(const SidebarFolder& folder) {
  model_->SetFolderCollapsed(folder.id, !folder.collapsed);
}

void TabListView::OnRenameFolder(FolderId id, const std::u16string& name) {
  model_->SetFolderName(id, name);
}

void TabListView::OnShowFolderMenu(FolderHeaderView* source,
                                   const SidebarFolder& folder,
                                   const gfx::Point& point) {
  context_menu_ = std::make_unique<RowContextMenu>(model_);
  context_menu_->RunForFolder(
      folder, source, point,
      base::BindRepeating(&FolderHeaderView::BeginRename,
                          source->GetWeakPtr()));
}

void TabListView::OnDropOnFolder(EntryId id, const SidebarFolder& folder) {
  model_->MoveEntryToFolder(id, folder.id);
}

bool TabListView::CanFolderAcceptEntry(EntryId id) const {
  // A folder holds this list's own entries. A favourite is drawn as a tile in
  // the grid and MoveEntryToFolder no-ops for it, so accepting the drop would
  // highlight the header and then discard the gesture; and an id the model
  // has dropped since the drag began is not a thing to file either. Both read
  // the same way here: no row of this list carries that id.
  //
  // A walk over the built rows, with no allocation: CanDrop is asked again
  // every time the pointer crosses a row boundary.
  if (section_ != SidebarSection::kPinned) {
    return false;
  }
  for (const TabRowView* row : rows_) {
    if (row->row().entry_id == id) {
      return true;
    }
  }
  return false;
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
  return section_ != SidebarSection::kFavorites &&
         RowDragData::Read(data).has_value();
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
  SetDropIndex(DropRowIndex(event.location().y()));
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
  const DropAnchor anchor =
      AnchorForDropIndex(DropRowIndex(event.location().y()));
  drag_payload_.reset();
  SetDropIndex(std::nullopt);
  if (!payload) {
    return base::NullCallback();
  }
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
  // entry still in it. ReorderEntry is lift-then-insert, so an entry already
  // *above* the boundary shifts everything below it up one when it is lifted
  // out and would overshoot by a slot — which is every downward drag, the
  // commonest one there is. An entry arriving from another section is not in
  // this count and needs no correction.
  const std::optional<int> from = EntryPositionInSection(id);
  return from && *from < to ? to - 1 : to;
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

BEGIN_METADATA(TabListView)
END_METADATA

}  // namespace arcium
