// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/second_launch.h"

#include "arcium/ui/browser/last_sidebar_window.h"
#include "arcium/ui/browser/page_in_space.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/startup/startup_tab.h"
#include "chrome/browser/ui/startup/startup_tab_provider.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_switches.h"
#include "ui/base/base_window.h"

namespace arcium {
namespace {

// Launches that mean something other than "show me the browser": a private
// window, an installed web app, a different profile. Each one asks for a
// window this one is not, so they go down Chromium's own path untouched.
bool AsksForAWindowOfItsOwn(const base::CommandLine& command_line) {
  return command_line.HasSwitch(switches::kIncognito) ||
         command_line.HasSwitch(switches::kApp) ||
         command_line.HasSwitch(switches::kAppId) ||
         command_line.HasSwitch(switches::kProfileDirectory) ||
         command_line.HasSwitch(switches::kProfileEmail);
}

}  // namespace

bool OpenSecondLaunchInExistingWindow(const base::CommandLine& command_line,
                                      const base::FilePath& cur_dir) {
  if (AsksForAWindowOfItsOwn(command_line)) {
    return false;
  }
  BrowserView* browser_view = LastActiveSidebarWindow();
  Browser* browser = browser_view ? browser_view->browser() : nullptr;
  if (!browser) {
    return false;
  }

  // Chromium's own parsing of what a command line asks to open, so a relative
  // path, a search term and a plain address are read here exactly as they
  // would be by the launch this replaces.
  const StartupTabs tabs = StartupTabProviderImpl().GetCommandLineTabs(
      command_line, cur_dir, browser->GetProfile());
  for (const StartupTab& tab : tabs) {
    // Routed, and so landing where the reader said that site belongs, the
    // same as the address they could have typed into the box instead.
    OpenUrlInRoutedSpace(browser, tab.url);
  }

  if (ui::BaseWindow* window = browser->GetWindow()) {
    window->Activate();
  }
  return true;
}

}  // namespace arcium
