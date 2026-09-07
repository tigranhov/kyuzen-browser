// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tab_list_view.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <utility>

#include "arcium/ui/sidebar/folder_header_view.h"
#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
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

TabListView::~TabListView() = default;

TabRowView* TabListView::MakeRow() {
  TabRowView::Delegate delegate;
  delegate.activate =
      base::BindRepeating(&TabListView::OnActivateRow, base::Unretained(this));
  delegate.close =
      base::BindRepeating(&TabListView::OnCloseRow, base::Unretained(this));
  delegate.drag_move =
      base::BindRepeating(&TabListView::OnDragMove, base::Unretained(this));
  delegate.rename =
      base::BindRepeating(&TabListView::OnRenameRow, base::Unretained(this));
  delegate.return_to_pinned_url =
      base::BindRepeating(&TabListView::OnRevertRow, base::Unretained(this));
  delegate.show_context_menu =
      base::BindRepeating(&TabListView::OnShowRowMenu, base::Unretained(this));
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
  for (const PlanItem& item : plan) {
    views::View* view = nullptr;
    if (item.is_header) {
      view = headers_[item.index];
    } else {
      TabRowView* row = rows_[next_row++];
      row->SetRow(*mine[item.index]);
      row->SetProperty(
          views::kMarginsKey,
          gfx::Insets::TLBR(0, item.indented ? metrics::kFolderIndent : 0, 0,
                            0));
      view = row;
    }
    ReorderChildView(view, child_index++);
  }

  SetVisible(!rows_.empty() || !headers_.empty() || new_tab_);
  InvalidateLayout();
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

void TabListView::OnDragMove(int from, int to) {
  // Clamp to this section's range in tab-index space. `rows_` is in laid-out
  // order — a folder's entries come before the top level — so the first and
  // last row are not the smallest and largest tab index, and a cold row has
  // no tab index at all. Taking the ends instead of the extremes hands
  // std::clamp lo > hi, which is a hard abort under libc++ hardening.
  //
  // A two-variable scan rather than a collected std::vector<int>: this runs
  // on every mouse-drag event while a row is being dragged, so an allocation
  // per pixel of motion is the 8ms-per-frame budget's kind of problem.
  int lo = std::numeric_limits<int>::max();
  int hi = std::numeric_limits<int>::min();
  for (const TabRowView* row : rows_) {
    const int index = row->tab_index();
    if (index >= 0) {
      lo = std::min(lo, index);
      hi = std::max(hi, index);
    }
  }
  // A section of nothing but cold rows has no tab-index range to move within,
  // and a cold row is not being dragged anywhere the tab strip understands.
  if (lo > hi || from < 0) {
    return;
  }
  to = std::clamp(to, lo, hi);
  if (to != from) {
    model_->MoveTab(from, to);
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

BEGIN_METADATA(TabListView)
END_METADATA

}  // namespace arcium
