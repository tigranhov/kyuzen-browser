// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/peek_controller.h"

#include <memory>
#include <utility>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/loose_page.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/new_tab_storage.h"
#include "arcium/ui/browser/page_in_space.h"
#include "arcium/ui/browser/peek_view.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "ui/compositor/layer.h"

namespace arcium {

PeekController::PeekController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  browser_view_->browser()->tab_strip_model()->AddObserver(this);
}

PeekController::~PeekController() {
  // The window is going away. Take the view down here rather than leaving it
  // for ~BrowserView's own children: this runs while the BrowserView is still
  // whole, and it hands the page back to the strip before the strip's tabs
  // are closed. The page's tab is left alone -- closing the window closes it
  // like any other tab.
  TearDown();
}

bool PeekController::Show(content::WebContents* source,
                          const content::OpenURLParams& params) {
  Browser* browser = browser_view_->browser();
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(browser->GetProfile());
  if (!state) {
    return false;
  }
  // The space of the tab the link was in, which is the answer a new tab
  // opened from it already gets: a peek from a Work pin reads Work's logins.
  const SpaceId space = SpaceForNewTab(
      *state->model(), *state->binding(),
      SpaceSwitcher::FromTabStripModel(browser->tab_strip_model()), source);
  if (!space.is_valid()) {
    return false;
  }
  Close();
  content::WebContents* page =
      OpenPageInSpace(browser, space, params, LoosePageKind::kPeek);
  if (!page) {
    return false;
  }
  Observe(page);
  PeekView::Actions actions;
  actions.close =
      base::BindRepeating(&PeekController::Close, weak_factory_.GetWeakPtr());
  actions.open_as_tab = base::BindRepeating(&PeekController::OpenAsTab,
                                            weak_factory_.GetWeakPtr());
  view_ = browser_view_->AddChildView(
      std::make_unique<PeekView>(std::move(actions)));
  // Above every other layer in the window, which is what "over the page"
  // means once both sides draw through layers. Added last is not enough on
  // its own: the window restacks its own children whenever it lays them out.
  if (ui::Layer* layer = view_->layer(); layer && layer->parent()) {
    layer->parent()->StackAtTop(layer);
  }
  view_->SetBoundsRect(page_area_);
  view_->SetPage(page);
  // The page was created hidden and its tab is never the active one, so
  // nothing else will ever tell it that it is on screen: without this the
  // renderer has no reason to paint the frame the peek is about to show.
  page->WasShown();
  view_->FocusPage();
  return true;
}

void PeekController::Close() {
  content::WebContents* page = web_contents();
  TearDown();
  if (page) {
    ClosePage(browser_view_->browser(), page);
  }
}

void PeekController::OpenAsTab() {
  content::WebContents* page = web_contents();
  TearDown();
  if (page) {
    BringPageToScreen(browser_view_->browser(), page);
  }
}

void PeekController::Layout(const gfx::Rect& page_area) {
  page_area_ = page_area;
  if (view_) {
    view_->SetBoundsRect(page_area_);
  }
}

void PeekController::WebContentsDestroyed() {
  // The page closed itself, or something else closed its tab: nothing left to
  // close, only the view to take down.
  TearDown();
}

void PeekController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // A peek belongs to the tab it was opened over. Going to another tab puts
  // it away, the way Zen's Glance closes with its parent. Posted, because the
  // strip does not allow a close from inside its own notification.
  if (!is_showing() || !selection.active_tab_changed()) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&PeekController::Close, weak_factory_.GetWeakPtr()));
}

void PeekController::TearDown() {
  Observe(nullptr);
  if (!view_) {
    return;
  }
  view_->SetPage(nullptr);
  PeekView* view = view_;
  view_ = nullptr;
  browser_view_->RemoveChildViewT(view);
}

bool ShowPeekForNavigation(content::WebContents* source,
                           const content::OpenURLParams& params) {
  tabs::TabInterface* tab =
      source ? tabs::TabInterface::MaybeGetFromContents(source) : nullptr;
  BrowserWindowInterface* window =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  // GetBrowserViewForBrowser goes through the native window, so a test window
  // that is not a BrowserView answers null rather than a bad cast.
  BrowserView* browser_view =
      window ? BrowserView::GetBrowserViewForBrowser(window) : nullptr;
  BrowserSidebarController* sidebar =
      browser_view ? browser_view->arcium_sidebar() : nullptr;
  return sidebar && sidebar->peek() && sidebar->peek()->Show(source, params);
}

}  // namespace arcium
