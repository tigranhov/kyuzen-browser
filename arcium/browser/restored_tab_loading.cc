// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/restored_tab_loading.h"

#include "arcium/common/arcium_features.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

namespace arcium {

namespace {

// The request BackgroundTabLoadingPolicy makes for a tab it has not loaded
// yet (background_tab_loading_policy.cc, ScheduleLoadForRestoredTabs): it
// reads the favicon database and does not load the page. A tab with no
// driver has nothing to ask.
void RequestFaviconFromDriver(content::WebContents* contents) {
  if (auto* driver = favicon::ContentFaviconDriver::FromWebContents(contents)) {
    driver->FetchFavicon(driver->GetActiveURL(), /*is_same_document=*/false);
  }
}

}  // namespace

bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs) {
  return DeferRestoredTabLoads(tabs,
                               base::BindRepeating(&RequestFaviconFromDriver));
}

bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs,
                           const FaviconRequest& request_favicon) {
  if (!features::IsNoLoadAtLaunchEnabled()) {
    return false;
  }
  for (content::WebContents* contents : tabs) {
    request_favicon.Run(contents);
  }
  return true;
}

bool IsTabUnloaded(content::WebContents* contents) {
  // Two flags because Chromium keeps two: a restored tab that has not loaded
  // reports NeedsReload(), while a discarded one reports WasDiscarded() and
  // deliberately not NeedsReload() (tab_lifecycle_unit.cc:246).
  return contents->GetController().NeedsReload() || contents->WasDiscarded();
}

}  // namespace arcium
