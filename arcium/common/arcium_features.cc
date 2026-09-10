// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/common/arcium_features.h"

#include <optional>
#include <string>

#include "base/command_line.h"
#include "base/time/time_delta_from_string.h"

namespace arcium::features {

BASE_FEATURE(kArciumSidebar, base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kArciumHomeBoundary, base::FEATURE_ENABLED_BY_DEFAULT);

base::TimeDelta FakeClockOffset() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(kFakeClockOffsetSwitch)) {
    return base::TimeDelta();
  }
  const std::optional<base::TimeDelta> offset = base::TimeDeltaFromString(
      command_line->GetSwitchValueASCII(kFakeClockOffsetSwitch));
  // is_inf() as well as is_positive(): "inf" parses, and an infinite offset
  // saturates every comparison it takes part in rather than moving the clock.
  if (!offset || !offset->is_positive() || offset->is_inf()) {
    return base::TimeDelta();
  }
  return *offset;
}

OffsetClock::OffsetClock(base::TimeDelta offset) : offset_(offset) {}

OffsetClock::~OffsetClock() = default;

base::Time OffsetClock::Now() const {
  return base::Time::Now() + offset_;
}

bool IsSidebarEnabled() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kNoSidebarSwitch)) {
    return false;
  }
  return base::FeatureList::IsEnabled(kArciumSidebar);
}

bool UsesImmersiveFullscreen() {
  return !IsSidebarEnabled();
}

bool IsHomeBoundaryEnabled() {
  return base::FeatureList::IsEnabled(kArciumHomeBoundary);
}

}  // namespace arcium::features
