// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/test/browser/sidebar_ui_browsertest_base.h"

#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/sidebar/extensions_row_view.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_desktop.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "extensions/common/extension.h"
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

ExtensionsRowView* SidebarUiTest::Row() {
  return Controller()->view()->extensions_row();
}

ExtensionsToolbarDesktop* SidebarUiTest::Container() {
  return BrowserView::GetBrowserViewForBrowser(browser())
      ->toolbar()
      ->extensions_container();
}

std::string SidebarUiTest::LoadTestExtension() {
  base::ScopedAllowBlockingForTesting allow_blocking;
  if (!extensions_dir_.IsValid()) {
    CHECK(extensions_dir_.CreateUniqueTempDir());
  }
  const std::string name = base::StrCat(
      {"arcium-pinned-", base::NumberToString(++extensions_written_)});
  const base::FilePath dir = extensions_dir_.GetPath().AppendASCII(name);
  CHECK(base::CreateDirectory(dir));
  // A button of its own is the whole point: without an action there is
  // nothing to pin.
  CHECK(base::WriteFile(
      dir.AppendASCII("manifest.json"),
      base::StrCat({"{\"manifest_version\": 3, \"name\": \"", name,
                    "\", \"version\": \"1.0\", \"action\": {\"default_popup\": "
                    "\"popup.html\"}}"})));
  CHECK(base::WriteFile(dir.AppendASCII("popup.html"),
                        "<!doctype html><title>popup</title><p>hello"));
  extensions::ChromeTestExtensionLoader loader(browser()->GetProfile());
  scoped_refptr<const extensions::Extension> extension =
      loader.LoadExtension(dir);
  CHECK(extension);
  return extension->id();
}

void SidebarUiTest::PinExtension(const std::string& id) {
  ToolbarActionsModel::Get(browser()->GetProfile())
      ->SetActionVisibility(id, true);
}

void SidebarUiTest::UnpinExtension(const std::string& id) {
  ToolbarActionsModel::Get(browser()->GetProfile())
      ->SetActionVisibility(id, false);
}

void SidebarUiTest::RunLoopUntilIdle() {
  base::RunLoop().RunUntilIdle();
}

}  // namespace arcium::test
