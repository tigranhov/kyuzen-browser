// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The list itself: the children it builds for one section, the order it lays
// them out in, and the commands its rows and folder headers issue. What a
// drag over this list is aimed at, and what a drop does when it lands, is in
// tab_list_drop.cc.

#include "arcium/ui/sidebar/tab_list_view.h"

#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "arcium/ui/sidebar/folder_header_view.h"
#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/split_grip_view.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

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

void TabListView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  // A split is one row: the second half is left out of the vertical layout,
  // and the two share the slot the first half was given.
  for (size_t i = 1; i < rows_.size(); ++i) {
    if (!rows_[i]->row().split_joins_previous ||
        !rows_[i - 1]->row().split_joins_next) {
      continue;
    }
    const gfx::Rect slot = rows_[i - 1]->bounds();
    const int left_width = (slot.width() - metrics::kSplitHalfGap) / 2;
    rows_[i - 1]->SetBounds(slot.x(), slot.y(), left_width, slot.height());
    rows_[i]->SetBounds(
        slot.x() + left_width + metrics::kSplitHalfGap, slot.y(),
        slot.right() - (slot.x() + left_width + metrics::kSplitHalfGap),
        slot.height());
  }
  // Each grip in the gap its row leaves between the halves.
  for (size_t g = 0; g < grips_.size() && g < grip_rows_.size(); ++g) {
    const TabRowView* first = rows_[grip_rows_[g]];
    grips_[g]->SetBounds(first->bounds().right(), first->y(),
                         metrics::kSplitHalfGap, first->height());
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
  delegate.hover_changed =
      base::BindRepeating(&TabListView::UpdateGrips, base::Unretained(this));
  return AddChildView(std::make_unique<TabRowView>(std::move(delegate)));
}

SplitGripView* TabListView::MakeGrip() {
  SplitGripView::Delegate delegate;
  delegate.drag_started = base::BindRepeating(&TabListView::OnRowDragStarted,
                                              base::Unretained(this));
  delegate.hover_changed =
      base::BindRepeating(&TabListView::UpdateGrips, base::Unretained(this));
  SplitGripView* grip =
      AddChildView(std::make_unique<SplitGripView>(std::move(delegate)));
  // Placed by Layout in the gap between a split row's halves.
  grip->SetProperty(views::kViewIgnoredByLayoutKey, true);
  return grip;
}

void TabListView::SetGrips() {
  grip_rows_.clear();
  for (size_t i = 0; i + 1 < rows_.size(); ++i) {
    if (rows_[i]->row().split_joins_next &&
        rows_[i + 1]->row().split_joins_previous) {
      grip_rows_.push_back(i);
    }
  }
  while (grips_.size() < grip_rows_.size()) {
    grips_.push_back(MakeGrip());
  }
  while (grips_.size() > grip_rows_.size()) {
    SplitGripView* grip = grips_.back();
    grips_.pop_back();
    RemoveChildViewT(grip);
  }
  for (size_t g = 0; g < grips_.size(); ++g) {
    const size_t first = grip_rows_[g];
    grips_[g]->SetPair(rows_[first]->row(), rows_[first + 1]->row());
    // Above the rows it sits between, so it is what the pointer finds there.
    ReorderChildView(grips_[g], children().size());
  }
  UpdateGrips();
}

void TabListView::UpdateGrips() {
  for (size_t g = 0; g < grips_.size() && g < grip_rows_.size(); ++g) {
    const size_t first = grip_rows_[g];
    grips_[g]->SetShown(grips_[g]->hovered() || rows_[first]->hovered() ||
                        rows_[first + 1]->hovered());
  }
}

std::unique_ptr<FolderHeaderView> TabListView::MakeHeader() {
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
  delegate.can_accept_folder = base::BindRepeating(
      &TabListView::CanFolderAcceptFolder, base::Unretained(this));
  delegate.drop_folder = base::BindRepeating(&TabListView::OnDropFolderOnFolder,
                                             base::Unretained(this));
  delegate.drag_started = base::BindRepeating(&TabListView::OnRowDragStarted,
                                              base::Unretained(this));
  // Not added to the child list here: SetRows adds the headers it draws, in
  // the plan's order, and leaves a hidden one out.
  return std::make_unique<FolderHeaderView>(std::move(delegate));
}

void TabListView::SetRows(const std::vector<SidebarRow>& all_rows) {
  // The rows are pooled, so a tint left on one would move to whatever it
  // holds next. The next drag move puts it back if it still belongs.
  SetSplitTarget(std::nullopt);
  std::vector<const SidebarRow*> mine;
  for (const SidebarRow& row : all_rows) {
    if (row.DrawnSection() == section_) {
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

  // The laid-out order. `folders` is pre-order with a depth on each, so one
  // linear walk lays the whole tree out: a folder's own entries follow its
  // header one level in, and a collapsed folder's subtree is exactly the run
  // of folders after it with a greater depth -- skipped here, headers and
  // rows alike, so a big collapsed tree costs nothing to draw.
  std::vector<PlanItem> plan;
  plan.reserve(mine.size() + folders.size());
  for (size_t f = 0; f < folders.size(); ++f) {
    plan.push_back({/*is_header=*/true, f, folders[f].depth});
    if (!folders[f].collapsed) {
      for (size_t r = 0; r < mine.size(); ++r) {
        if (mine[r]->folder_id == folders[f].id) {
          plan.push_back({/*is_header=*/false, r, folders[f].depth + 1});
        }
      }
      continue;
    }
    const int depth = folders[f].depth;
    while (f + 1 < folders.size() && folders[f + 1].depth > depth) {
      ++f;
    }
  }
  for (size_t r = 0; r < mine.size(); ++r) {
    // A row naming a folder this section did not get back is drawn at the top
    // level rather than lost: the model is the authority on which folders
    // exist, and a stale id must not hide a tab. A row inside a *hidden*
    // folder is a different case and is not drawn here -- its folder is
    // known, it is just collapsed away.
    if (!mine[r]->folder_id.has_value() || !known.count(*mine[r]->folder_id)) {
      plan.push_back({/*is_header=*/false, r, /*depth=*/0});
    }
  }

  size_t rows_needed = 0;
  // Which folders the plan draws. A folder inside a collapsed one is still in
  // `folders`, and still gets a header built for it, but must not be a child
  // of this list while it is hidden.
  std::vector<bool> drawn(folders.size(), false);
  for (const PlanItem& item : plan) {
    rows_needed += item.is_header ? 0u : 1u;
    if (item.is_header) {
      drawn[item.index] = true;
    }
  }

  // Grow or shrink the pools, then assign in order. Views are reused by
  // position so a title change or reorder does not allocate.
  while (headers_.size() < folders.size()) {
    headers_.push_back(MakeHeader());
  }
  while (headers_.size() > folders.size()) {
    // The pool owns it, so dropping it deletes it; ~View takes it out of the
    // child list on the way if it was in one.
    headers_.pop_back();
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
    // Taken out here rather than during the walk below, so the walk sees the
    // child list the plan describes and nothing else.
    if (!drawn[f] && headers_[f]->parent() == this) {
      RemoveChildView(headers_[f].get());
    }
  }
  // One walk assigns the data and puts the children in the plan's order;
  // BoxLayout lays them out by child index.
  size_t next_row = 0;
  size_t child_index = 0;
  row_positions_.clear();
  row_positions_.reserve(rows_needed);
  // Positions count this section's own rows only. A row drawn here from
  // another section has no place in this section's order, so it takes the
  // place of the next row that does.
  std::vector<int> own_position(mine.size(), 0);
  int own_rows = 0;
  for (size_t r = 0; r < mine.size(); ++r) {
    own_position[r] = own_rows;
    if (mine[r]->section == section_) {
      ++own_rows;
    }
  }
  section_row_count_ = own_rows;
  for (const PlanItem& item : plan) {
    views::View* view = nullptr;
    if (item.is_header) {
      FolderHeaderView* header = headers_[item.index].get();
      // Already a child unless it was hidden inside a collapsed folder until
      // now: re-added rather than rebuilt, and never detached and reattached
      // in the same pass, which would take the focus off a rename in flight.
      if (header->parent() != this) {
        AddChildViewRaw(header);
      }
      view = header;
    } else {
      TabRowView* row = rows_[next_row++];
      // `mine` is in the order the model hands the section over, which for an
      // entry section is position order. The plan is not, so the position
      // travels with the row rather than being read off its slot later.
      row_positions_.push_back(own_position[item.index]);
      row->SetRow(*mine[item.index]);
      // The second half of a split shares the first half's slot, which
      // Layout divides between them.
      row->SetProperty(views::kViewIgnoredByLayoutKey,
                       mine[item.index]->split_joins_previous);
      view = row;
    }
    // One rule for both kinds of child: the plan already worked out that a
    // header sits at its folder's depth and a row one level further in.
    view->SetProperty(
        views::kMarginsKey,
        gfx::Insets::TLBR(0, item.depth * metrics::kFolderIndent, 0, 0));
    ReorderChildView(view, child_index++);
  }
  SetGrips();

  UpdateVisibility();
  InvalidateLayout();
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

void TabListView::OnRenameRow(const SidebarRow& row,
                              const std::u16string& title) {
  if (row.entry_id.is_valid()) {
    model_->SetEntryTitle(row.entry_id, title);
  } else if (row.tab_index >= 0) {
    // The index is checked against the URL the row held when the edit opened,
    // because a strip can shift between the Enter and the posted commit --
    // the archive service closes idle Today tabs without the user touching
    // anything. A mismatch means the slot now holds a different page, and the
    // rename is dropped rather than landing on it.
    model_->SetTabTitle(row.tab_index, row.url, title);
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

void TabListView::OnDropOnFolder(const RowDragData& payload,
                                 const SidebarFolder& folder) {
  // A half filed on its own leaves its split; a pair dragged by its grip
  // goes in whole, which the model's pair rule does for a pinned pair.
  if (!payload.split_pair) {
    model_->LeaveSplit(payload.entry_id, payload.tab_index);
  }
  model_->MoveEntryToFolder(payload.entry_id, folder.id);
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

bool TabListView::CanFolderAcceptFolder(FolderId id, FolderId parent) const {
  return section_ == SidebarSection::kPinned &&
         model_->CanMoveFolderTo(id, parent);
}

void TabListView::OnDropFolderOnFolder(FolderId id,
                                       const SidebarFolder& folder) {
  model_->SetFolderParent(id, folder.id);
}

BEGIN_METADATA(TabListView)
END_METADATA

}  // namespace arcium
