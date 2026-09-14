// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/test/browser/sidebar_ui_browsertest_base.h"

#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "net/dns/mock_host_resolver.h"

namespace arcium::test {

SidebarUiTest::SidebarUiTest()
    : no_reveal_animation_(UrlPillView::DisableRevealAnimationForTesting()) {}

SidebarUiTest::~SidebarUiTest() = default;

void SidebarUiTest::SetUpOnMainThread() {
  InProcessBrowserTest::SetUpOnMainThread();
  host_resolver()->AddRule("*", "127.0.0.1");
}

BrowserSidebarController* SidebarUiTest::Controller() {
  return BrowserView::GetBrowserViewForBrowser(browser())->arcium_sidebar();
}

UrlPillView* SidebarUiTest::Pill() {
  return Controller()->view()->url_pill();
}

}  // namespace arcium::test
