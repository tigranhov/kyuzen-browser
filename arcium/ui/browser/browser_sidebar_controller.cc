// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/browser_sidebar_controller.h"

#include <memory>
#include <utility>

#include "arcium/common/arcium_features.h"
#include "arcium/ui/browser/quick_entry_bubble.h"
#include "arcium/ui/sidebar/nav_row_view.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "arcium/ui/sidebar/view_snapshot.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "content/public/browser/web_contents.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {

// static
std::unique_ptr<BrowserSidebarController> BrowserSidebarController::MaybeCreate(
    BrowserView* browser_view) {
  if (!features::IsSidebarEnabled() ||
      !browser_view->browser()->is_type_normal()) {
    return nullptr;
  }
  return base::WrapUnique(new BrowserSidebarController(browser_view));
}

BrowserSidebarController::BrowserSidebarController(BrowserView* browser_view)
    : browser_view_(browser_view),
      model_(std::make_unique<SidebarTabModel>(
          browser_view->browser()->tab_strip_model())) {
  SidebarView::Delegate delegate;
  delegate.toggle_sidebar = base::BindRepeating(
      &BrowserSidebarController::ToggleVisibility, base::Unretained(this));
  delegate.back =
      base::BindRepeating(&BrowserSidebarController::ExecuteCommand,
                          base::Unretained(this), IDC_BACK);
  delegate.forward =
      base::BindRepeating(&BrowserSidebarController::ExecuteCommand,
                          base::Unretained(this), IDC_FORWARD);
  delegate.reload =
      base::BindRepeating(&BrowserSidebarController::ExecuteCommand,
                          base::Unretained(this), IDC_RELOAD);
  delegate.edit_url =
      base::BindRepeating(&BrowserSidebarController::ExecuteCommand,
                          base::Unretained(this), IDC_FOCUS_LOCATION);
  view_ = browser_view_->AddChildView(
      std::make_unique<SidebarView>(model_.get(), std::move(delegate)));
  model_->AddObserver(this);
  UpdateNavButtons();
  MaybeScheduleSnapshot();
  MaybeShowQuickEntryForDebugging();
}

BrowserSidebarController::~BrowserSidebarController() {
  model_->RemoveObserver(this);
}

int BrowserSidebarController::width() const {
  return visible_ ? metrics::kSidebarWidth : 0;
}

void BrowserSidebarController::AdjustLayoutParams(BrowserLayoutParams& params) {
  // Remember the frame's caption-button area so the nav row leaves room for
  // the traffic lights, then take the sidebar column off the leading edge and
  // inset the rest so the page floats on the tinted frame.
  // GetBrowserLayoutParams is called several times per layout pass, so only
  // touch the view when the value changes; SetCaptionButtonWidth invalidates
  // layout and would otherwise loop.
  const int caption =
      static_cast<int>(params.leading_exclusion.ContentWithPadding().width());
  if (caption != caption_button_width_) {
    caption_button_width_ = caption;
    view_->SetCaptionButtonWidth(caption_button_width_);
  }
  params.InsetHorizontal(width(), /*leading=*/true);
  params.leading_exclusion = BrowserLayoutExclusionArea();
  if (visible_) {
    params.Inset(gfx::Insets::TLBR(metrics::kContentInset, 0,
                                   metrics::kContentInset,
                                   metrics::kContentInset));
  }
}

void BrowserSidebarController::LayoutSidebar(const gfx::Rect& host_bounds) {
  view_->SetVisible(visible_);
  view_->SetBoundsRect(gfx::Rect(host_bounds.x(), host_bounds.y(), width(),
                                 host_bounds.height()));
  UpdateContentCorners();
}

void BrowserSidebarController::UpdateContentCorners() {
  ContentsContainerView* container =
      browser_view_->GetActiveContentsContainerView();
  if (!container) {
    return;
  }
  const float r = visible_ ? metrics::kContentCornerRadius : 0.f;
  if (r == applied_corner_radius_ && container == last_container_) {
    return;  // Called on every layout; do not re-apply unchanged radii.
  }
  applied_corner_radius_ = r;
  last_container_ = container;
  container->SetRoundedCorners(gfx::RoundedCornersF(r, r, r, r));
}

bool BrowserSidebarController::IsPositionInWindowCaption(
    const gfx::Point& point_in_browser_view) const {
  if (!visible_ || !view_->bounds().Contains(point_in_browser_view)) {
    return false;
  }
  gfx::Point p = point_in_browser_view;
  views::View::ConvertPointToTarget(browser_view_, view_, &p);
  return view_->IsPositionInWindowCaption(p);
}

void BrowserSidebarController::ToggleVisibility() {
  visible_ = !visible_;
  browser_view_->InvalidateLayout();
}

void BrowserSidebarController::HostLocationBar() {
  LocationBarView* bar = browser_view_->GetLocationBarView();
  if (!bar || !bar->parent() || view_->url_pill()->has_hosted_view()) {
    return;
  }
  // The toolbar keeps its pointer and delegate role; only the view moves.
  std::unique_ptr<views::View> owned = bar->parent()->RemoveChildViewT(bar);
  view_->url_pill()->SetHostedView(std::move(owned));
}

void BrowserSidebarController::ShowQuickEntry() {
  if (quick_entry_widget_) {
    quick_entry_->FocusField();
    return;
  }
  quick_entry_ = std::make_unique<QuickEntryBubble>(
      browser_view_,
      base::BindOnce(&BrowserSidebarController::OnQuickEntrySubmitted,
                     weak_factory_.GetWeakPtr()));
  quick_entry_widget_ = views::BubbleDialogDelegate::CreateBubble(
      quick_entry_.get(),
      base::BindOnce(&BrowserSidebarController::OnQuickEntryClosed,
                     weak_factory_.GetWeakPtr()));
  quick_entry_widget_->Show();
  quick_entry_->FocusField();
}

void BrowserSidebarController::OnQuickEntrySubmitted(
    const std::u16string& text) {
  AutocompleteMatch match;
  AutocompleteClassifierFactory::GetForProfile(browser_view_->GetProfile())
      ->Classify(text, /*in_keyword_mode=*/false,
                 /*allow_exact_keyword_match=*/false,
                 ::metrics::OmniboxEventProto::INVALID_SPEC, &match,
                 /*alternate_nav_url=*/nullptr);
  if (match.destination_url.is_valid()) {
    chrome::AddSelectedTabWithURL(browser_view_->browser(),
                                  match.destination_url,
                                  ui::PAGE_TRANSITION_TYPED);
  }
}

void BrowserSidebarController::OnQuickEntryClosed(
    views::Widget::ClosedReason reason) {
  // Runs synchronously from the close; free both once the stack unwinds.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserSidebarController::DestroyQuickEntry,
                                weak_factory_.GetWeakPtr()));
}

void BrowserSidebarController::DestroyQuickEntry() {
  quick_entry_widget_.reset();
  quick_entry_.reset();
}

void BrowserSidebarController::OnSidebarModelChanged() {
  UpdateNavButtons();
}

void BrowserSidebarController::UpdateNavButtons() {
  // No active tab while the window is being built; chrome::CanGoForward does
  // not check for that.
  content::WebContents* contents =
      browser_view_->browser()->tab_strip_model()->GetActiveWebContents();
  view_->nav_row()->SetBackEnabled(contents && chrome::CanGoBack(contents));
  view_->nav_row()->SetForwardEnabled(contents &&
                                      chrome::CanGoForward(contents));
}

void BrowserSidebarController::MaybeScheduleSnapshot() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(features::kSnapshotSwitch)) {
    return;
  }
  int delay_seconds = 4;
  base::StringToInt(
      command_line->GetSwitchValueASCII(features::kSnapshotDelaySwitch),
      &delay_seconds);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BrowserSidebarController::WriteSnapshot,
                     weak_factory_.GetWeakPtr(),
                     command_line->GetSwitchValuePath(features::kSnapshotSwitch)),
      base::Seconds(delay_seconds > 0 ? delay_seconds : 4));
}

void BrowserSidebarController::MaybeShowQuickEntryForDebugging() {
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          features::kQuickEntrySwitch)) {
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BrowserSidebarController::ShowQuickEntry,
                     weak_factory_.GetWeakPtr()),
      base::Seconds(2));
}

void BrowserSidebarController::WriteSnapshot(const base::FilePath& path) {
  LogViewHierarchy(browser_view_);
  WriteViewSnapshot(browser_view_, path, /*scale=*/2.f, base::DoNothing());
}

void BrowserSidebarController::ExecuteCommand(int command_id) {
  chrome::ExecuteCommand(browser_view_->browser(), command_id);
}

bool HandleNewTabCommand(Browser* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->arcium_sidebar()) {
    return false;
  }
  browser_view->arcium_sidebar()->ShowQuickEntry();
  return true;
}

}  // namespace arcium
