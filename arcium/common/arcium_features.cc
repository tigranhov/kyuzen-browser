// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/common/arcium_features.h"

#include "base/command_line.h"

namespace arcium::features {

BASE_FEATURE(kArciumSidebar, base::FEATURE_ENABLED_BY_DEFAULT);

bool IsSidebarEnabled() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kNoSidebarSwitch)) {
    return false;
  }
  return base::FeatureList::IsEnabled(kArciumSidebar);
}

}  // namespace arcium::features
