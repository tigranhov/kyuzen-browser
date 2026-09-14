// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/browser_sidebar_controller.h"

#include <memory>
#include <utility>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/common/arcium_features.h"
#include "arcium/ui/browser/command_box.h"
#include "arcium/ui/browser/session_rebuild_nudge.h"
#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/sidebar/extensions_row_view.h"
#include "arcium/ui/sidebar/nav_row_view.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "arcium/ui/sidebar/view_snapshot.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/bubble_anchor_util.h"
#include "chrome/browser/ui/page_info/page_info_dialog.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_button.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_desktop.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/security_state/content/security_state_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
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
    : browser_view_(browser_view) {
  // One model, binding and store per profile, not per window: a second
  // window on the same profile must see the same entries. GetForBrowserContext
  // also starts the (asynchronous) load the first time it is asked.
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(browser_view->GetProfile());
  // Every warm/cold transition has to reach the session file, and the only
  // thing that writes it is a command rebuild. Installed here because this is
  // where the profile and the binding first meet on the //chrome side; it is
  // per profile and idempotent, so a second window re-installs the same thing.
  InstallSessionRebuildNudge(browser_view->GetProfile(), state->binding());
  // Before the model and the service, both of which hold a bare pointer to
  // it, and before the sidebar view, which draws whatever space it names.
  space_switcher_ = std::make_unique<SpaceSwitcher>(
      browser_view_->browser()->tab_strip_model(), state->model(),
      state->binding());
  space_switcher_->SetBlankTabCallback(
      base::BindRepeating(&BrowserSidebarController::ShowCommandBoxWithNoText,
                          weak_factory_.GetWeakPtr()));
  model_ = std::make_unique<SidebarTabModel>(
      browser_view->browser()->tab_strip_model(), state->model(),
      state->binding(), space_switcher_.get());
  // One archive per profile (it is one SQLite file), one service per window
  // (a tab is in exactly one strip). Off the record there is neither: see
  // ArciumProfileState::archive().
  if (state->archive()) {
    archive_service_ = std::make_unique<ArchiveService>(
        browser_view->browser()->tab_strip_model(), state->model(),
        state->binding(), state->archive(), state->archive_runner(),
        &archive_clock_, space_switcher_.get());
    model_->SetArchiveService(archive_service_.get());
  }
  SidebarView::Delegate delegate;
  delegate.toggle_sidebar = base::BindRepeating(
      &BrowserSidebarController::ToggleVisibility, base::Unretained(this));
  delegate.back = base::BindRepeating(&BrowserSidebarController::ExecuteCommand,
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
  delegate.open_extensions =
      base::BindRepeating(&BrowserSidebarController::OpenExtensionsMenu,
                          weak_factory_.GetWeakPtr());
  delegate.copy_link = base::BindRepeating(
      &BrowserSidebarController::CopyCurrentUrl, weak_factory_.GetWeakPtr());
  delegate.open_site_info = base::BindRepeating(
      &BrowserSidebarController::ShowSiteInfo, weak_factory_.GetWeakPtr());
  view_ = browser_view_->AddChildView(
      std::make_unique<SidebarView>(model_.get(), std::move(delegate)));
  model_->AddObserver(this);
  UpdateNavButtons();
  MaybeScheduleSnapshot();
  MaybeShowCommandBoxForDebugging();
}

BrowserSidebarController::~BrowserSidebarController() {
  model_->RemoveObserver(this);
  // `model_` outlives `archive_service_` by declaration order, and holds a
  // pointer to it. Break that before the service is freed.
  model_->SetArchiveService(nullptr);
  archive_service_.reset();
}

int BrowserSidebarController::width() const {
  return visible_ ? metrics::kSidebarWidth : 0;
}

int BrowserSidebarController::TitlebarHeight() const {
  return metrics::kTitlebarHeight;
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

void BrowserSidebarController::HostExtensionsContainer() {
  ToolbarView* toolbar = browser_view_->toolbar();
  if (!toolbar || !toolbar->extensions_container() ||
      view_->extensions_row()->has_hosted_view()) {
    return;
  }
  // The toolbar keeps its pointer and its accessor, which is how extension
  // popups still find their owner; only the view moves. The same trade the
  // location bar makes one row up.
  views::View* container = toolbar->extensions_container();
  if (!container->parent()) {
    return;
  }
  std::unique_ptr<views::View> owned =
      container->parent()->RemoveChildViewT(container);
  view_->extensions_row()->SetHostedView(std::move(owned));
}

void BrowserSidebarController::ShowCommandBoxWithNoText() {
  ShowCommandBox(std::nullopt);
}

void BrowserSidebarController::ShowCommandBox(
    std::optional<std::u16string> initial_text) {
  if (command_box_widget_) {
    command_box_->FocusField();
    if (initial_text) {
      command_box_->SetText(*initial_text, /*select_all=*/true);
    }
    return;
  }
  suggestion_source_ =
      std::make_unique<SuggestionSource>(browser_view_->GetProfile());
  command_box_ = std::make_unique<CommandBox>(
      browser_view_, suggestion_source_.get(),
      base::BindOnce(&BrowserSidebarController::OnCommandBoxAccepted,
                     weak_factory_.GetWeakPtr()));
  command_box_widget_ = views::BubbleDialogDelegate::CreateBubble(
      command_box_.get(),
      base::BindOnce(&BrowserSidebarController::OnCommandBoxClosed,
                     weak_factory_.GetWeakPtr()));
  command_box_widget_->Show();
  if (initial_text) {
    command_box_->SetText(*initial_text, /*select_all=*/true);
  }
  command_box_->FocusField();
}

void BrowserSidebarController::OnCommandBoxAccepted(SuggestionRow row) {
  if (!row.destination.is_valid()) {
    return;
  }
  // A tab already open is switched to rather than loaded a second time, in
  // whatever space is holding it.
  if (row.is_open_tab && ActivateTabWithUrl(row.destination)) {
    return;
  }
  chrome::AddSelectedTabWithURL(browser_view_->browser(), row.destination,
                                ui::PAGE_TRANSITION_TYPED);
}

bool BrowserSidebarController::ActivateTabWithUrl(const GURL& url) {
  TabStripModel* strip = browser_view_->browser()->tab_strip_model();
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!contents || contents->GetLastCommittedURL() != url) {
      continue;
    }
    // The tab may live in a space this window is not showing; going to the
    // tab without going to its space would show it under the wrong sidebar.
    if (space_switcher_) {
      space_switcher_->SwitchTo(space_switcher_->SpaceOfTabAt(i));
    }
    strip->ActivateTabAt(i);
    return true;
  }
  return false;
}

void BrowserSidebarController::OnCommandBoxClosed(
    views::Widget::ClosedReason reason) {
  // Runs synchronously from the close; free them once the stack unwinds.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserSidebarController::DestroyCommandBox,
                                weak_factory_.GetWeakPtr()));
}

void BrowserSidebarController::DestroyCommandBox() {
  command_box_widget_.reset();
  command_box_.reset();
  // With the box goes everything it was asking on behalf of.
  suggestion_source_.reset();
}

void BrowserSidebarController::OnSidebarModelChanged() {
  UpdateNavButtons();
  UpdatePillForActiveTab();
}

void BrowserSidebarController::DidChangeVisibleSecurityState() {
  UpdatePillSecurity();
}

void BrowserSidebarController::PrimaryPageChanged(content::Page& page) {
  UpdatePillForActiveTab();
}

void BrowserSidebarController::UpdatePillForActiveTab() {
  content::WebContents* contents =
      browser_view_->browser()->tab_strip_model()->GetActiveWebContents();
  // Watch whichever tab is on screen, so a page that turns insecure while it
  // sits there is not read once at navigation and then trusted forever.
  if (contents != web_contents()) {
    Observe(contents);
  }
  view_->url_pill()->SetUrl(contents ? contents->GetLastCommittedURL()
                                     : GURL());
  UpdatePillSecurity();
}

void BrowserSidebarController::UpdatePillSecurity() {
  content::WebContents* contents = web_contents();
  auto* helper =
      contents ? SecurityStateTabHelper::FromWebContents(contents) : nullptr;
  const security_state::SecurityLevel level =
      helper ? helper->GetSecurityLevel() : security_state::NONE;
  // NONE is an internal page or a data URL: neither secure nor an accusation.
  // Only the two levels that exist to be warned about count as insecure.
  const bool insecure =
      level == security_state::WARNING || level == security_state::DANGEROUS;
  view_->url_pill()->SetConnectionSecure(!insecure);
}

void BrowserSidebarController::OpenExtensionsMenu() {
  ToolbarView* toolbar = browser_view_->toolbar();
  if (!toolbar || !toolbar->extensions_container()) {
    return;
  }
  toolbar->extensions_container()
      ->GetExtensionsButton()
      ->ToggleExtensionsMenu();
}

void BrowserSidebarController::CopyCurrentUrl() {
  content::WebContents* contents =
      browser_view_->browser()->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return;
  }
  // The pill shows a domain and this gives the whole address, which is the
  // point of having both.
  ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
      .WriteText(base::UTF8ToUTF16(contents->GetLastCommittedURL().spec()));
}

void BrowserSidebarController::ShowSiteInfo() {
  content::WebContents* contents =
      browser_view_->browser()->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return;
  }
  // Chrome's own page information, not a panel of ours: it is a security
  // surface Chromium already writes, maintains and translates.
  ShowPageInfoDialog(contents, base::DoNothing(),
                     bubble_anchor_util::Anchor::kLocationBar);
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
      base::BindOnce(
          &BrowserSidebarController::WriteSnapshot, weak_factory_.GetWeakPtr(),
          command_line->GetSwitchValuePath(features::kSnapshotSwitch)),
      base::Seconds(delay_seconds > 0 ? delay_seconds : 4));
}

void BrowserSidebarController::MaybeShowCommandBoxForDebugging() {
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          features::kQuickEntrySwitch)) {
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BrowserSidebarController::ShowCommandBoxWithNoText,
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

int ExtensionsDisplayMode(Browser* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  const bool has_sidebar = browser_view && browser_view->arcium_sidebar();
  return static_cast<int>(has_sidebar
                              ? ExtensionsToolbarDesktop::DisplayMode::kAutoHide
                              : ExtensionsToolbarDesktop::DisplayMode::kNormal);
}

bool HandleNewTabCommand(Browser* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->arcium_sidebar()) {
    return false;
  }
  browser_view->arcium_sidebar()->ShowCommandBox(std::nullopt);
  return true;
}

}  // namespace arcium
