// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_PARTITION_GUARD_THROTTLE_H_
#define ARCIUM_UI_BROWSER_PARTITION_GUARD_THROTTLE_H_

#include "arcium/browser/model/entry_id.h"
#include "content/public/browser/navigation_throttle.h"

namespace arcium {

// The last word on which storage a page loads in. Every creation hook picks
// a tab's storage when the tab is made; this checks the result on every
// main-frame navigation of a tab in the sidebar, and when a page would land
// in the wrong storage -- a popup content created without an opener, a
// browser page in a profile's tab, or any path a future Chromium adds --
// cancels it and opens the same address in a tab of the right kind.
//
// The reopen goes through WebContents::OpenURL, so the new tab is created by
// the same hook as every other new tab, and carries a POST body and the rest
// of the navigation with it.
class PartitionGuardThrottle : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  PartitionGuardThrottle(content::NavigationThrottleRegistry& registry,
                         ProfileId space_profile);
  PartitionGuardThrottle(const PartitionGuardThrottle&) = delete;
  PartitionGuardThrottle& operator=(const PartitionGuardThrottle&) = delete;
  ~PartitionGuardThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult CheckStorage();

  // The profile of the space the tab belonged to when the navigation
  // started. Read once: a profile change reopens every tab of the space, so
  // a tab whose space changes mid-navigation is already gone.
  const ProfileId space_profile_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_PARTITION_GUARD_THROTTLE_H_
