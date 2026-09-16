// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/last_sidebar_window.h"

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/views/frame/browser_view.h"

namespace arcium {

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

}  // namespace arcium
