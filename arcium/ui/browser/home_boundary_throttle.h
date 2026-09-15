// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_HOME_BOUNDARY_THROTTLE_H_
#define ARCIUM_UI_BROWSER_HOME_BOUNDARY_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"
#include "url/gurl.h"

namespace content {
class NavigationThrottleRegistry;
}

namespace arcium {

// Keeps a pinned or favourite entry on its home site: a link click to another
// host opens in a peek over the entry, or in a new tab where no peek can be
// shown, instead of navigating the entry away.
//
// Carries no policy. The rule is arcium::LinkLeavesHome; this class only
// decides which navigations the rule is asked about, and performs the
// diversion when it says yes.
class HomeBoundaryThrottle : public content::NavigationThrottle {
 public:
  // Adds a throttle only when the feature is on and this navigation is in the
  // primary main frame of a tab bound to a pinned or favourite entry.
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  HomeBoundaryThrottle(content::NavigationThrottleRegistry& registry,
                       GURL home);
  HomeBoundaryThrottle(const HomeBoundaryThrottle&) = delete;
  HomeBoundaryThrottle& operator=(const HomeBoundaryThrottle&) = delete;
  ~HomeBoundaryThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  const char* GetNameForLogging() override;

 private:
  // The entry's stored URL, read once when the throttle was created. Held
  // rather than looked up again so that the decision cannot straddle a model
  // change mid-navigation.
  const GURL home_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_HOME_BOUNDARY_THROTTLE_H_
