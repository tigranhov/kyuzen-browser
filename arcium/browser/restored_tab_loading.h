// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_RESTORED_TAB_LOADING_H_
#define ARCIUM_BROWSER_RESTORED_TAB_LOADING_H_

#include <vector>

#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace arcium {

// Asks for one tab's favicon without loading its page.
using FaviconRequest = base::RepeatingCallback<void(content::WebContents*)>;

// Takes the restored tabs session restore would hand to Chromium's
// background loader (patch 0170). When Arcium's no-load-at-launch rule is on
// it asks for each tab's favicon -- the one useful thing the loader did for a
// tab it had not reached yet -- loads none of them, and returns true. Returns
// false, touching nothing, when the rule is off, so the caller goes on to
// Chromium's own loader.
bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs);

// The same, with the favicon request supplied: the rule lives here, and the
// overload above only binds the real favicon driver, so a test can see the
// request made.
bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs,
                           const FaviconRequest& request_favicon);

// A tab exists but its page is not in memory: restored and not yet loaded,
// or discarded to save memory. Clicking it loads the page it was left on.
bool IsTabUnloaded(content::WebContents* contents);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_RESTORED_TAB_LOADING_H_
