// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/entry_claim.h"

#include <optional>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"

namespace arcium {

bool IsClaimedByEntry(const ArciumModel& model,
                      const TabBinding& binding,
                      tabs::TabHandle handle) {
  const std::optional<EntryId> id = binding.EntryForTab(handle);
  return id.has_value() && model.GetEntry(*id) != nullptr;
}

}  // namespace arcium
