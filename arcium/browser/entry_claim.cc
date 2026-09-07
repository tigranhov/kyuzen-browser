// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/entry_claim.h"

#include <optional>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"

namespace arcium {

bool IsClaimedByEntry(const ArciumModel& model,
                      const TabBinding& binding,
                      tabs::TabHandle handle) {
  const std::optional<EntryId> id = binding.EntryForTab(handle);
  if (!id.has_value()) {
    return false;
  }
  const TabEntry* entry = model.GetEntry(*id);
  // Scoped to the space whose rows are actually drawn. GetEntry searches every
  // space, and an entry of some other space claiming this tab would keep it
  // out of Today while nothing put it anywhere else — the same invisible,
  // unarchivable tab a stale binding gives, one field further along.
  return entry && entry->space_id == model.default_space_id();
}

}  // namespace arcium
