// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/outside_links.h"

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/common/arcium_features.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/browser/outside_link_window.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// The window the reader was last in that has a sidebar, because that is the
// window a link from another application belongs beside and the window whose
// space "the space on screen" means. Activation order is the right order
// here for exactly that reason.
BrowserView* LastActiveSidebarWindow() {
  BrowserView* found = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&found](BrowserWindowInterface* window) {
        BrowserView* browser_view =
            BrowserView::GetBrowserViewForBrowser(window);
        if (browser_view && browser_view->arcium_sidebar()) {
          found = browser_view;
          return false;
        }
        return true;
      });
  return found;
}

}  // namespace

bool OpenOutsideLinks(const std::vector<GURL>& urls) {
  if (!features::IsOutsideLinkWindowEnabled() || urls.empty()) {
    return false;
  }
  // All or nothing: a `file:` URL or a `chromium://` scheme among them sends
  // the whole set down Chromium's own path, which knows what to do with each.
  for (const GURL& url : urls) {
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
      return false;
    }
  }
  BrowserView* browser_view = LastActiveSidebarWindow();
  Browser* browser = browser_view ? browser_view->browser() : nullptr;
  if (!browser) {
    return false;
  }
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(browser->GetProfile());
  const SpaceSwitcher* switcher =
      SpaceSwitcher::FromTabStripModel(browser->tab_strip_model());
  if (!state || !switcher) {
    return false;
  }
  for (const GURL& url : urls) {
    // A rule's space, else the space the window is showing -- the two
    // answers R4.5 gives for where a link the reader did not open in a tab
    // belongs.
    SpaceId space = state->model()->SpaceForUrl(url);
    if (!space.is_valid()) {
      space = switcher->active_space();
    }
    OutsideLinkWindow::Open(browser, space, url);
  }
  return true;
}

}  // namespace arcium
