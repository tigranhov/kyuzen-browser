// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/sidebar_view.h"

#include <memory>
#include <optional>
#include <utility>

#include "arcium/ui/sidebar/archive_list_view.h"
#include "arcium/ui/sidebar/favorites_grid_view.h"
#include "arcium/ui/sidebar/nav_row_view.h"
#include "arcium/ui/sidebar/section_divider_view.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/space_bar_view.h"
#include "arcium/ui/sidebar/tab_list_view.h"
#include "arcium/ui/sidebar/tint_background.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

SidebarView::SidebarView(SidebarModel* model, Delegate delegate)
    : model_(model), delegate_(std::move(delegate)) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kVertical)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch)
      .SetInteriorMargin(gfx::Insets(metrics::kSidebarPadding))
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(3, 0));
  SetBackground(std::make_unique<TintBackground>());

  NavRowView::Delegate nav;
  nav.toggle_sidebar = delegate_.toggle_sidebar;
  nav.back = delegate_.back;
  nav.forward = delegate_.forward;
  nav.reload = delegate_.reload;
  nav_row_ = AddChildView(std::make_unique<NavRowView>(std::move(nav)));
  url_pill_ = AddChildView(std::make_unique<UrlPillView>(delegate_.edit_url));
  favorites_ = AddChildView(std::make_unique<FavoritesGridView>(model_));
  pinned_ = AddChildView(
      std::make_unique<TabListView>(model_, SidebarSection::kPinned));
  // No archive, no archive button. Off the record there is no archive file
  // and cannot be one, so the affordance is absent rather than disabled; see
  // SidebarModel::has_archive().
  divider_ = AddChildView(std::make_unique<SectionDividerView>(
      base::BindRepeating(&SidebarModel::ClearToday, base::Unretained(model_)),
      model_->has_archive() ? base::BindRepeating(&SidebarView::ShowArchiveList,
                                                  base::Unretained(this))
                            : base::RepeatingClosure()));
  // Today scrolls: with enough tabs the rows would otherwise be laid out past
  // the bottom of the column at zero height, which hides them entirely.
  // ScrollWithLayers is the macOS default, but a layer-backed viewport is not
  // opaque over the sidebar gradient (views::Label DCHECKs) and hides the rows
  // from the offscreen paint that --snapshot uses.
  today_scroll_ = AddChildView(std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kDisabled));
  today_ = today_scroll_->SetContents(
      std::make_unique<TabListView>(model_, SidebarSection::kToday));
  // Without a height clamp the viewport never sizes its contents (ScrollView
  // only does that for a bounded scroll view or a layer-backed one), so the
  // rows stay at zero. The upper bound is the whole column; FlexLayout gives
  // the scroll view whatever height is left after the sections above it.
  today_scroll_->ClipHeightTo(0, 100000);
  today_scroll_->SetBackgroundColor(std::nullopt);
  today_scroll_->SetDrawOverflowIndicator(false);
  today_scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  today_scroll_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  // Absorbs the leftover height, so the space bar stays at the bottom.
  today_scroll_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kVertical,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  space_bar_ = AddChildView(std::make_unique<SpaceBarView>(model_));

  // Every section is a drag source, and the two that can be empty are targets
  // that only exist while a drag is running; see RowDragSession.
  favorites_->SetDragSession(&row_drag_session_);
  pinned_->SetDragSession(&row_drag_session_);
  today_->SetDragSession(&row_drag_session_);

  // Cmd+Shift+Backspace returns the active pinned entry to its pinned URL,
  // the same command the row's revert button issues. It is an accelerator
  // rather than a key handler on the row because sidebar rows are only
  // accessibility-focusable, so there is normally no focused row to press it
  // on; the active row is the one the user is looking at.
  AddAccelerator(
      ui::Accelerator(ui::VKEY_BACK, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN));

  observation_.Observe(model_);
  Rebuild();
}

SidebarView::~SidebarView() {
  // ~View destroys the children, and it runs after this object's own members
  // are gone. The sections must stop observing the session while it is still
  // there.
  favorites_->SetDragSession(nullptr);
  pinned_->SetDragSession(nullptr);
  today_->SetDragSession(nullptr);
}

void SidebarView::SetCaptionButtonWidth(int width) {
  caption_button_width_ = width;
  nav_row_->SetLeadingInset(width);
}

bool SidebarView::IsPositionInWindowCaption(const gfx::Point& point) const {
  if (nav_row_->bounds().Contains(point)) {
    gfx::Point p = point;
    ConvertPointToTarget(this, nav_row_, &p);
    return nav_row_->IsPointOnBackground(p);
  }
  // Empty space below the last row and above the space bar drags the window.
  const int last_row_bottom =
      today_scroll_->y() + today_->GetPreferredSize().height();
  return point.y() > last_row_bottom && point.y() < space_bar_->y();
}

void SidebarView::OnSidebarModelChanged() {
  Rebuild();
}

gfx::Size SidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(metrics::kSidebarWidth, available_size.height().value_or(0));
}

bool SidebarView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  for (const SidebarRow& row : model_->rows()) {
    if (row.is_active && row.can_return_to_pinned_url &&
        row.entry_id.is_valid()) {
      model_->ReturnToPinnedUrl(row.entry_id);
      return true;
    }
  }
  return false;
}

void SidebarView::ShowArchiveList() {
  // A bubble closes on deactivate, and the press that reaches this button has
  // already deactivated the open one — so in practice this is a fresh open
  // every time. The guard is for the paths that are not a mouse press: an
  // accessibility action, or a test firing the button twice.
  if (archive_widget_) {
    archive_widget_->Close();
    return;
  }
  // Anchored to the divider rather than to the button that opened it: the
  // button is a hover affordance and hides itself the moment the bubble takes
  // activation, and an anchor view that disappears takes the bubble's
  // position with it.
  archive_list_ = std::make_unique<ArchiveListView>(divider_, model_);
  archive_widget_ = views::BubbleDialogDelegate::CreateBubble(
      archive_list_.get(), base::BindOnce(&SidebarView::OnArchiveListClosed,
                                          weak_factory_.GetWeakPtr()));
  archive_widget_->Show();
}

void SidebarView::OnArchiveListClosed(views::Widget::ClosedReason reason) {
  // Runs synchronously from inside the close; free both once the stack has
  // unwound, the way BrowserSidebarController does for quick entry.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarView::DestroyArchiveList,
                                weak_factory_.GetWeakPtr()));
}

void SidebarView::DestroyArchiveList() {
  archive_widget_.reset();
  archive_list_.reset();
}

void SidebarView::Rebuild() {
  // Rebuilt wholesale on every change: cheap at tens of rows, and the model
  // coalesces bursts. Each section reuses its row views by position.
  const std::vector<SidebarRow> rows = model_->rows();
  favorites_->SetRows(rows);
  pinned_->SetRows(rows);
  today_->SetRows(rows);
  if (!url_pill_->has_hosted_view()) {
    for (const SidebarRow& row : rows) {
      if (row.is_active) {
        url_pill_->SetPlaceholderText(base::UTF8ToUTF16(row.url.host()));
        break;
      }
    }
  }
  InvalidateLayout();
}

BEGIN_METADATA(SidebarView)
END_METADATA

}  // namespace arcium
