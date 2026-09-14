// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/sidebar_view.h"

#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

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
#include "base/time/time.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/event_handler.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

namespace {

// How long the column takes to slide in when the space changes.
constexpr base::TimeDelta kSwitchSlideDuration = base::Milliseconds(200);

// How far a sideways gesture travels, momentum included, before it switches
// spaces: a quarter of the sidebar. Which gestures are sideways is decided by
// their first movement, not by this; see SidebarView::OnScrollEvent.
constexpr float kSwipeThreshold = metrics::kSidebarWidth / 4;

}  // namespace

// Hands SidebarView the scroll events headed for any view inside it, before
// that view sees them. The column is a layer-backed ScrollView, and on macOS
// it passes every scroll to the compositor, which reports each one handled,
// so a swipe over the rows never bubbles up to the sidebar. Only scrolls: the
// view itself registered as a pre-target handler would also be sent every
// descendant's mouse and key events, which View's own handlers act on.
class SidebarView::ScrollForwarder : public ui::EventHandler {
 public:
  explicit ScrollForwarder(SidebarView* view) : view_(view) {}

  // ui::EventHandler:
  void OnScrollEvent(ui::ScrollEvent* event) override {
    // Over the sidebar's own background the sidebar is the target and is
    // handed the event anyway; reading it here too would count it twice.
    if (event->target() == view_.get()) {
      return;
    }
    view_->OnScrollEvent(event);
  }

 private:
  raw_ptr<SidebarView> view_;
};

SidebarView::SidebarView(SidebarModel* model, Delegate delegate)
    : model_(model), delegate_(std::move(delegate)) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kVertical)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch)
      .SetInteriorMargin(gfx::Insets(metrics::kSidebarPadding))
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(3, 0));
  auto tint = std::make_unique<TintBackground>();
  tint_ = tint.get();
  SetBackground(std::move(tint));

  NavRowView::Delegate nav;
  nav.toggle_sidebar = delegate_.toggle_sidebar;
  nav.back = delegate_.back;
  nav.forward = delegate_.forward;
  nav.reload = delegate_.reload;
  nav_row_ = AddChildView(std::make_unique<NavRowView>(std::move(nav)));
  url_pill_ = AddChildView(std::make_unique<UrlPillView>(delegate_.edit_url));
  favorites_ = AddChildView(std::make_unique<FavoritesGridView>(model_));

  // Pinned, the divider and Today scroll as one column. Pinned outside the
  // viewport would keep its full height whatever else needed the room, so
  // enough pins push Today off the bottom of the panel and it cannot be
  // reached at all. Favourites, the nav row and the space bar stay put:
  // those are the fixed frame the scrolling column sits in.
  scroll_ = AddChildView(std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kEnabled));
  auto* column = scroll_->SetContents(std::make_unique<views::View>());
  auto* column_layout =
      column->SetLayoutManager(std::make_unique<views::FlexLayout>());
  column_layout->SetOrientation(views::LayoutOrientation::kVertical)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch)
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(3, 0));

  pinned_ = column->AddChildView(
      std::make_unique<TabListView>(model_, SidebarSection::kPinned));
  // No archive, no archive button. Off the record there is no archive file
  // and cannot be one, so the affordance is absent rather than disabled; see
  // SidebarModel::has_archive().
  divider_ = column->AddChildView(std::make_unique<SectionDividerView>(
      base::BindRepeating(&SidebarModel::ClearToday, base::Unretained(model_)),
      model_->has_archive() ? base::BindRepeating(&SidebarView::ShowArchiveList,
                                                  base::Unretained(this))
                            : base::RepeatingClosure()));
  today_ = column->AddChildView(
      std::make_unique<TabListView>(model_, SidebarSection::kToday));

  // Layers, which is the macOS default: kUiCompositorScrollWithLayers is
  // enabled there, so the compositor owns a scroll input handler and
  // ScrollView::OnScrollEvent DCHECKs scroll_with_layers_enabled_. Without
  // them the first two-finger scroll over the sidebar aborts the browser.
  // The cost is paid elsewhere: every Label inside disables subpixel
  // rendering, because the layer is not opaque over the sidebar gradient,
  // and --snapshot cannot see these rows, because its offscreen paint skips
  // layer-backed views.
  //
  // Without a height clamp the viewport never sizes its contents (ScrollView
  // only does that for a bounded scroll view or a layer-backed one), so the
  // rows stay at zero. The upper bound is the whole column; FlexLayout gives
  // the scroll view whatever height is left after favourites and the nav row.
  scroll_->ClipHeightTo(0, 100000);
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetDrawOverflowIndicator(false);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  // Absorbs the leftover height, so the space bar stays at the bottom.
  scroll_->SetProperty(
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
  // Ctrl+1 to Ctrl+9 show the nth space in the bar. Like the one above, they
  // arrive only when the page has not consumed the key.
  for (int i = 0; i < 9; ++i) {
    AddAccelerator(ui::Accelerator(
        static_cast<ui::KeyboardCode>(ui::VKEY_1 + i), ui::EF_CONTROL_DOWN));
  }

  scroll_forwarder_ = std::make_unique<ScrollForwarder>(this);
  AddPreTargetHandler(scroll_forwarder_.get());

  observation_.Observe(model_);
  Rebuild();
}

SidebarView::~SidebarView() {
  RemovePreTargetHandler(scroll_forwarder_.get());
  // ~View destroys the children, and it runs after this object's own members
  // are gone. The sections must stop observing the session while it is still
  // there.
  favorites_->SetDragSession(nullptr);
  pinned_->SetDragSession(nullptr);
  today_->SetDragSession(nullptr);
}

int SidebarView::tint_preset_for_testing() const {
  return tint_->preset();
}

views::View* SidebarView::column_for_testing() {
  return scroll_->contents();
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
  // The column's bottom is where its content ends once the scroll offset is
  // taken off; when the content overflows, that lands below the viewport and
  // there is no empty space to drag by, which this comparison gives for free.
  const int content_bottom = scroll_->y() +
                             scroll_->contents()->GetPreferredSize().height() -
                             scroll_->GetVisibleRect().y();
  return point.y() > content_bottom && point.y() < space_bar_->y();
}

void SidebarView::OnSidebarModelChanged() {
  const SpaceId was_shown = shown_space_;
  const size_t was_index = shown_space_index_;
  Rebuild();
  // Only a switch slides. Everything else this hears -- a title, a favicon,
  // a load -- rebuilds in place, and most of what it hears is that.
  // A space that is gone has no place in the bar to slide from: the
  // placeholder the window starts on before the model file is read, or the
  // space a delete just removed.
  if (was_shown.is_valid() && shown_space_ != was_shown &&
      shown_space_was_kept_) {
    SlideColumnIn(/*from_trailing=*/shown_space_index_ > was_index);
  }
}

gfx::Size SidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(metrics::kSidebarWidth, available_size.height().value_or(0));
}

bool SidebarView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.IsCtrlDown() && accelerator.key_code() >= ui::VKEY_1 &&
      accelerator.key_code() <= ui::VKEY_9) {
    const size_t index = accelerator.key_code() - ui::VKEY_1;
    const std::vector<SidebarSpace> spaces = model_->spaces();
    // Past the last space this is not ours: swallowing it would make a
    // shortcut that does nothing, which reads as a broken key.
    if (index >= spaces.size()) {
      return false;
    }
    model_->SwitchToSpace(spaces[index].id);
    return true;
  }
  for (const SidebarRow& row : model_->rows()) {
    if (row.is_active && row.can_return_to_pinned_url &&
        row.entry_id.is_valid()) {
      model_->ReturnToPinnedUrl(row.entry_id);
      return true;
    }
  }
  return false;
}

void SidebarView::OnScrollEvent(ui::ScrollEvent* event) {
  // A gesture's direction is fixed by its first movement, as Chromium's
  // history swipe fixes it. Fingers scrolling a long list drift sideways, and
  // the drift adds up, so judging each event alone would switch spaces in the
  // middle of a scroll, and would take the sideways-leaning events from the
  // column, dropping their vertical part so the list stutters.
  //
  // The fingers landing begin a gesture. macOS says so with the momentum
  // phase, MAY_BEGIN, and leaves the scroll phase at kNone throughout;
  // elsewhere the scroll phase says kBegan.
  const ui::EventMomentumPhase momentum = event->momentum_phase();
  const bool begins =
      event->scroll_event_phase() == ui::ScrollEventPhase::kBegan ||
      momentum == ui::EventMomentumPhase::MAY_BEGIN;
  // Momentum runs on after the fingers lift and belongs to the gesture they
  // made: it neither starts a gesture nor decides which way one goes.
  const bool coasting = momentum == ui::EventMomentumPhase::BEGAN ||
                        momentum == ui::EventMomentumPhase::INERTIAL_UPDATE;
  // Anything else once the fingers are up -- the momentum's own end, a wheel,
  // a gesture that began outside the sidebar -- starts afresh, so nothing of
  // the last gesture carries into it.
  if (begins || (!swipe_fingers_down_ && !coasting)) {
    swipe_axis_ = SwipeAxis::kUnknown;
    swipe_offset_ = 0;
    swipe_spent_ = false;
    swipe_fingers_down_ = true;
  }
  const float dx = event->x_offset();
  const float dy = event->y_offset();
  if (swipe_axis_ == SwipeAxis::kUnknown && !coasting && (dx != 0 || dy != 0)) {
    swipe_axis_ = std::abs(dx) > std::abs(dy) ? SwipeAxis::kHorizontal
                                              : SwipeAxis::kVertical;
  }
  // The fingers lift. Not a reset: momentum may follow, and it is still this
  // gesture's. On macOS the momentum's end reads the same, which is why the
  // reset waits for the next event that is not momentum.
  if (event->scroll_event_phase() == ui::ScrollEventPhase::kEnd ||
      momentum == ui::EventMomentumPhase::END) {
    swipe_fingers_down_ = false;
  }
  if (swipe_axis_ != SwipeAxis::kHorizontal) {
    // Vertical, or not moving yet: the column's to scroll.
    return;
  }
  // Nothing in the sidebar scrolls sideways, so every event of a sideways
  // gesture is the swipe's, whatever it is over.
  event->SetHandled();
  event->StopPropagation();
  if (swipe_spent_) {
    return;
  }
  swipe_offset_ += dx;
  if (std::abs(swipe_offset_) < kSwipeThreshold) {
    return;
  }
  // One gesture switches once, momentum included. A positive offset is
  // fingers moving right, which goes back a space, the way a page turns.
  swipe_spent_ = true;
  SwitchToNeighbour(swipe_offset_ > 0 ? -1 : 1);
}

void SidebarView::SwitchToNeighbour(int step) {
  const std::vector<SidebarSpace> spaces = model_->spaces();
  for (size_t i = 0; i < spaces.size(); ++i) {
    if (!spaces[i].is_active) {
      continue;
    }
    const int target = static_cast<int>(i) + step;
    if (target >= 0 && static_cast<size_t>(target) < spaces.size()) {
      model_->SwitchToSpace(spaces[target].id);
    }
    return;
  }
}

void SidebarView::SlideColumnIn(bool from_trailing) {
  ui::Layer* layer = scroll_->contents()->layer();
  if (!layer) {
    return;
  }
  // The animator ticks only while a slide runs and detaches from the
  // compositor when it ends, so there is nothing left running between
  // switches. A switch during a slide restarts it from the side.
  ui::LayerAnimator* animator = layer->GetAnimator();
  animator->StopAnimating();
  // Slides from, and settles back on, whatever the layer rests at. That is
  // the identity except right to left, where ScrollView keeps a flip on this
  // same layer, and a slide to the identity would leave the rows mirrored.
  const gfx::Transform rest = layer->GetTargetTransform();
  gfx::Transform start = rest;
  start.PostTranslate(
      from_trailing ? metrics::kSidebarWidth : -metrics::kSidebarWidth, 0);
  layer->SetTransform(start);
  ui::ScopedLayerAnimationSettings settings(animator);
  settings.SetTransitionDuration(kSwitchSlideDuration);
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  layer->SetTransform(rest);
}

void SidebarView::ShowArchiveList() {
  // A bubble closes on deactivate, and the press that reaches this button has
  // already deactivated the open one — so in practice this is a fresh open
  // every time. The guard is for the paths that are not a mouse press: an
  // accessibility action, or a test firing the button twice.
  //
  // It reuses the widget rather than closing it, which is what
  // BrowserSidebarController::ShowQuickEntry does with its bubble. Closing
  // was wrong twice over: CreateBubble arms the close through
  // MakeCloseSynchronous, whose override_close_ is a OnceCallback the first
  // close consumes, so a second Close() before DestroyArchiveList runs falls
  // through Widget's deprecated asynchronous path on a CLIENT_OWNS_WIDGET
  // widget — and the archive_widget_.reset() already queued then lands on top
  // of whatever that started.
  if (archive_widget_) {
    if (!archive_closing_) {
      archive_widget_->Show();
    }
    // Already closing: DestroyArchiveList is queued and the next press opens
    // a fresh bubble. Building one here would overwrite archive_list_ while
    // the closing widget still points at it.
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
  archive_closing_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarView::DestroyArchiveList,
                                weak_factory_.GetWeakPtr()));
}

void SidebarView::DestroyArchiveList() {
  archive_widget_.reset();
  archive_list_.reset();
  archive_closing_ = false;
}

void SidebarView::Rebuild() {
  // Rebuilt wholesale on every change: cheap at tens of rows, and the model
  // coalesces bursts. Each section reuses its row views by position.
  // The tint is the space on screen's, and remembering which space that is
  // is how OnSidebarModelChanged tells a switch from any other change.
  int preset = 0;
  const std::vector<SidebarSpace> spaces = model_->spaces();
  const SpaceId was_shown = shown_space_;
  shown_space_was_kept_ = false;
  bool found_active = false;
  for (size_t i = 0; i < spaces.size(); ++i) {
    if (spaces[i].id == was_shown) {
      shown_space_was_kept_ = true;
    }
    if (spaces[i].is_active && !found_active) {
      found_active = true;
      preset = spaces[i].gradient;
      shown_space_ = spaces[i].id;
      shown_space_index_ = i;
    }
  }
  const int painted = tint_->preset();
  tint_->SetPreset(preset);
  if (tint_->preset() != painted) {
    SchedulePaint();
  }

  const std::vector<SidebarRow> rows = model_->rows();
  favorites_->SetRows(rows);
  pinned_->SetRows(rows);
  today_->SetRows(rows);
  if (!url_pill_->has_hosted_view()) {
    for (const SidebarRow& row : rows) {
      if (row.is_active) {
        url_pill_->SetUrl(row.url);
        break;
      }
    }
  }
  InvalidateLayout();
}

BEGIN_METADATA(SidebarView)
END_METADATA

}  // namespace arcium
