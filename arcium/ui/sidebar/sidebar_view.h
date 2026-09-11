// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_VIEW_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_VIEW_H_

#include <memory>

#include "arcium/ui/sidebar/row_drag_session.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ui {
class ScrollEvent;
}

namespace views {
class ScrollView;
}

namespace arcium {

class ArchiveListView;
class NavRowView;
class UrlPillView;
class FavoritesGridView;
class TabListView;
class SectionDividerView;
class SpaceBarView;
class TintBackground;

// The sidebar column: nav rows, favourites, pinned, divider, today, space bar.
// Pure Views; everything it shows comes from SidebarModel.
class SidebarView : public views::View, public SidebarModel::Observer {
  METADATA_HEADER(SidebarView, views::View)

 public:
  // Window-level actions the sidebar triggers but does not implement.
  struct Delegate {
    base::RepeatingClosure toggle_sidebar;
    base::RepeatingClosure back;
    base::RepeatingClosure forward;
    base::RepeatingClosure reload;
    // Clicking the URL pill placeholder (no hosted location bar).
    base::RepeatingClosure edit_url;
  };

  SidebarView(SidebarModel* model, Delegate delegate);
  SidebarView(const SidebarView&) = delete;
  SidebarView& operator=(const SidebarView&) = delete;
  ~SidebarView() override;

  // Width of the frame-owned controls (macOS traffic lights) that the first
  // row must leave empty on the leading side.
  void SetCaptionButtonWidth(int width);

  // The slot the browser puts the real location bar into (Task 10). Null in
  // the playground, where UrlPillView paints a placeholder.
  UrlPillView* url_pill() { return url_pill_; }
  NavRowView* nav_row() { return nav_row_; }
  SectionDividerView* divider() { return divider_; }
  // The open archive bubble's delegate, or null when none is open.
  ArchiveListView* archive_list_for_testing() { return archive_list_.get(); }
  // The preset the background paints, which follows the active space.
  int tint_preset_for_testing() const;
  // The column that slides on a space switch.
  views::View* column_for_testing();

  // True for points in the sidebar that should drag the window: the nav row
  // background and any empty space, but not buttons or tab rows.
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

  // SidebarModel::Observer:
  void OnSidebarModelChanged() override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  // A two-finger sideways swipe switches to the neighbouring space. Reached
  // for scrolls over any view inside the sidebar, not only over its own
  // background; see ScrollForwarder.
  void OnScrollEvent(ui::ScrollEvent* event) override;

 private:
  class ScrollForwarder;

  void Rebuild();
  // Switches to the space `step` places along the bar from the active one,
  // or does nothing past either end.
  void SwitchToNeighbour(int step);
  // Slides the column in from the side the newly shown space sits on in the
  // bar: from the trailing edge when it is to the right of the one before.
  void SlideColumnIn(bool from_trailing);
  // Opens the archive list over the divider. Only ever reached from the
  // divider's archive button, which exists only when the model has an
  // archive.
  void ShowArchiveList();
  void OnArchiveListClosed(views::Widget::ClosedReason reason);
  void DestroyArchiveList();

  raw_ptr<SidebarModel> model_;
  Delegate delegate_;
  base::ScopedObservation<SidebarModel, SidebarModel::Observer> observation_{
      this};

  int caption_button_width_ = 0;

  // Owned by this view as its background.
  raw_ptr<TintBackground> tint_ = nullptr;
  // The space the last rebuild drew, and its place in the bar, so that a
  // change can tell a switch, which slides, from everything else, which
  // does not.
  SpaceId shown_space_;
  size_t shown_space_index_ = 0;
  // Sideways travel since the current trackpad gesture began, and whether
  // that gesture has already switched.
  float swipe_offset_ = 0;
  bool swipe_spent_ = false;
  std::unique_ptr<ScrollForwarder> scroll_forwarder_;

  raw_ptr<NavRowView> nav_row_ = nullptr;
  raw_ptr<UrlPillView> url_pill_ = nullptr;
  raw_ptr<FavoritesGridView> favorites_ = nullptr;
  raw_ptr<TabListView> pinned_ = nullptr;
  raw_ptr<SectionDividerView> divider_ = nullptr;
  // Holds Pinned, the divider and Today; they scroll as one column.
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<TabListView> today_ = nullptr;
  raw_ptr<SpaceBarView> space_bar_ = nullptr;
  // Shared by the grid and both lists, and detached from all three in the
  // destructor: members are destroyed before ~View destroys the children, so
  // a section still observing this when it goes would be a dangling observer.
  RowDragSession row_drag_session_;

  // The archive bubble, when one is open. CreateBubble hands back a widget
  // the caller owns and a delegate the caller must keep alive for at least as
  // long, so both live here and are freed together, one turn after the close
  // — the same shape as BrowserSidebarController's quick-entry bubble, and
  // for the same reason: the close callback runs from inside the close.
  //
  // In this order, because members are destroyed in reverse: the widget goes
  // before the delegate it points at, and both go before ~View destroys the
  // divider the bubble is anchored to.
  std::unique_ptr<ArchiveListView> archive_list_;
  std::unique_ptr<views::Widget> archive_widget_;
  // Set between the close and the posted DestroyArchiveList that frees the
  // pair. The widget is non-null throughout that window but must not be shown
  // again, and Widget::IsClosed() cannot be asked: CreateBubble's
  // MakeCloseSynchronous means CloseWithReason runs the override and returns
  // before it ever sets `widget_closed_`.
  bool archive_closing_ = false;
  base::WeakPtrFactory<SidebarView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_VIEW_H_
