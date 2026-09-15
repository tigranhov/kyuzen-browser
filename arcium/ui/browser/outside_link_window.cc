// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/outside_link_window.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/loose_page.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/space.h"
#include "arcium/ui/browser/page_in_space.h"
#include "arcium/ui/sidebar/pill_domain.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// Arc's proportions for the same window, which is the reference here: tall
// enough for a page to be read, narrow enough to be plainly not the browser.
constexpr int kWidth = 480;
constexpr int kHeight = 640;
constexpr int kScreenMargin = 24;
// Down and to the left per window already open, because the first one sits
// against the top right corner and there is nowhere else to go.
constexpr int kCascade = 24;

// Every open window. Owns them: a window removes itself from here when its
// widget goes away, and that is what frees it.
std::vector<std::unique_ptr<OutsideLinkWindow>>& OpenWindows() {
  static base::NoDestructor<std::vector<std::unique_ptr<OutsideLinkWindow>>>
      windows;
  return *windows;
}

void ForgetWindow(OutsideLinkWindow* window) {
  std::erase_if(OpenWindows(),
                [window](const std::unique_ptr<OutsideLinkWindow>& open) {
                  return open.get() == window;
                });
}

// What the window holds: the page's domain and the one button on a row, and
// the page under it. A view of its own rather than views::WidgetDelegateView,
// whose constructor upstream is private to a closed friend list.
class OutsideLinkContents : public views::View {
  METADATA_HEADER(OutsideLinkContents, views::View)

 public:
  OutsideLinkContents(const std::u16string& domain,
                      const std::u16string& button_text,
                      base::RepeatingClosure open_in_space,
                      base::RepeatingClosure close)
      : close_(std::move(close)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));

    auto header = std::make_unique<views::View>();
    auto* header_layout =
        header->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(8, 12),
            8));
    views::Label* label =
        header->AddChildView(std::make_unique<views::Label>(domain));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    header_layout->SetFlexForView(label, 1);
    header->AddChildView(std::make_unique<views::MdTextButton>(
        std::move(open_in_space), button_text));
    AddChildView(std::move(header));

    web_view_ = AddChildView(std::make_unique<views::WebView>());
    layout->SetFlexForView(web_view_, 1);

    // The window's own Cmd+W. Whether macOS lets it answer before the main
    // menu's Close tab does is the open question in the stage's design.
    AddAccelerator(ui::Accelerator(ui::VKEY_W, ui::EF_COMMAND_DOWN));
  }
  OutsideLinkContents(const OutsideLinkContents&) = delete;
  OutsideLinkContents& operator=(const OutsideLinkContents&) = delete;
  ~OutsideLinkContents() override = default;

  views::WebView* web_view() { return web_view_; }

  // views::View:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    if (accelerator.key_code() != ui::VKEY_W) {
      return false;
    }
    // On its own turn: closing the widget takes this view with it, and a
    // view must not be deleted inside its own event handling.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, close_);
    return true;
  }

 private:
  raw_ptr<views::WebView> web_view_ = nullptr;
  base::RepeatingClosure close_;
};

BEGIN_METADATA(OutsideLinkContents)
END_METADATA

// 480 by 640 at the top right of the screen `browser`'s window is on, moved
// down and left by one step per window already open.
gfx::Rect BoundsForWindow(Browser* browser, size_t cascade_index) {
  gfx::Rect area(0, 0, kWidth + 2 * kScreenMargin, kHeight + 2 * kScreenMargin);
  if (display::Screen* screen = display::Screen::GetScreen()) {
    BrowserWindow* window = browser ? browser->window() : nullptr;
    area = window ? screen->GetDisplayNearestWindow(window->GetNativeWindow())
                        .work_area()
                  : screen->GetPrimaryDisplay().work_area();
  }
  const int step = static_cast<int>(cascade_index) * kCascade;
  return gfx::Rect(area.right() - kWidth - kScreenMargin - step,
                   area.y() + kScreenMargin + step, kWidth, kHeight);
}

}  // namespace

// static
void OutsideLinkWindow::Open(Browser* browser, SpaceId space, const GURL& url) {
  if (!browser) {
    return;
  }
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(browser->GetProfile());
  const Space* target = state ? state->model()->GetSpace(space) : nullptr;
  if (!target) {
    return;
  }
  content::OpenURLParams params(url, content::Referrer(),
                                WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                ui::PAGE_TRANSITION_TYPED,
                                /*is_renderer_initiated=*/false);
  content::WebContents* page =
      OpenPageInSpace(browser, space, params, LoosePageKind::kOutsideLink);
  if (!page) {
    return;
  }
  const size_t cascade_index = OpenWindows().size();
  OpenWindows().push_back(base::WrapUnique(
      new OutsideLinkWindow(browser, page, target->name, url, cascade_index)));
}

// static
size_t OutsideLinkWindow::CountForTesting() {
  return OpenWindows().size();
}

// static
OutsideLinkWindow* OutsideLinkWindow::LastForTesting() {
  return OpenWindows().empty() ? nullptr : OpenWindows().back().get();
}

OutsideLinkWindow::OutsideLinkWindow(Browser* browser,
                                     content::WebContents* page,
                                     const std::u16string& space_name,
                                     const GURL& url,
                                     size_t cascade_index)
    : browser_(browser->AsWeakPtr()) {
  Observe(page);
  auto contents = std::make_unique<OutsideLinkContents>(
      PillDomain(url), base::StrCat({u"Open in ", space_name}),
      base::BindRepeating(&OutsideLinkWindow::OpenInSpace,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&OutsideLinkWindow::Close,
                          weak_factory_.GetWeakPtr()));
  OutsideLinkContents* contents_view = contents.get();

  delegate_ = std::make_unique<views::WidgetDelegate>();
  delegate_->SetTitle(PillDomain(url));
  delegate_->SetCanResize(true);
  delegate_->SetContentsView(std::move(contents));

  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate_.get();
  params.bounds = BoundsForWindow(browser, cascade_index);
  params.name = "ArciumOutsideLink";
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  widget_->AddObserver(this);

  web_view_ = contents_view->web_view();
  web_view_->SetWebContents(page);
  // The page was created hidden and its tab is never the active one in the
  // main window, so nothing else would ever tell it that it is on screen.
  page->WasShown();
  widget_->Show();
}

OutsideLinkWindow::~OutsideLinkWindow() {
  if (widget_) {
    widget_->RemoveObserver(this);
  }
}

void OutsideLinkWindow::OpenInSpace() {
  content::WebContents* page = web_contents();
  if (!page || !browser_) {
    Close();
    return;
  }
  // Set first: the close below must leave the page where it now is.
  promoted_ = true;
  Observe(nullptr);
  if (web_view_) {
    web_view_->SetWebContents(nullptr);
  }
  BringPageToScreen(browser_.get(), page);
  if (BrowserWindow* window = browser_->window()) {
    window->Activate();
  }
  Close();
}

void OutsideLinkWindow::Close() {
  if (closing_) {
    return;
  }
  closing_ = true;
  widget_->Close();
}

void OutsideLinkWindow::OnWidgetDestroying(views::Widget* widget) {
  widget_->RemoveObserver(this);
  closing_ = true;
  content::WebContents* page = web_contents();
  Observe(nullptr);
  if (web_view_) {
    web_view_->SetWebContents(nullptr);
    web_view_ = nullptr;
  }
  // Closing the window closes the page, unless the button has just put the
  // page in the main window.
  if (page && !promoted_ && browser_) {
    ClosePage(browser_.get(), page);
  }
  DeleteSoon();
}

void OutsideLinkWindow::WebContentsDestroyed() {
  // The page's tab went away under the window: the main window closed, or
  // something else closed the tab. There is nothing left to show.
  if (web_view_) {
    web_view_->SetWebContents(nullptr);
  }
  Close();
}

void OutsideLinkWindow::DeleteSoon() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ForgetWindow, base::Unretained(this)));
}

}  // namespace arcium
