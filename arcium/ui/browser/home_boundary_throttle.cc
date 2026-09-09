// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/home_boundary_throttle.h"

#include <utility>

#include "content/public/browser/navigation_throttle_registry.h"

namespace arcium {

HomeBoundaryThrottle::HomeBoundaryThrottle(
    content::NavigationThrottleRegistry& registry,
    GURL home)
    : content::NavigationThrottle(registry), home_(std::move(home)) {}

HomeBoundaryThrottle::~HomeBoundaryThrottle() = default;

const char* HomeBoundaryThrottle::GetNameForLogging() {
  return "HomeBoundaryThrottle";
}

// static
void HomeBoundaryThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {}

content::NavigationThrottle::ThrottleCheckResult
HomeBoundaryThrottle::WillStartRequest() {
  return content::NavigationThrottle::PROCEED;
}

}  // namespace arcium
