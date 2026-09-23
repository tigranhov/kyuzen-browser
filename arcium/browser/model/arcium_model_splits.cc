// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pinned splits: two pinned entries linked so that the sidebar draws them as
// one and a relaunch brings them back as one. The link itself lives here;
// the operations in arcium_model.cc that move an entry ask PairOf so that a
// linked entry never moves without its partner.

#include <algorithm>
#include <vector>

#include "arcium/browser/model/arcium_model.h"

namespace arcium {

bool ArciumModel::LinkSplitEntries(EntryId a, EntryId b) {
  const TabEntry* first = GetEntry(a);
  const TabEntry* second = GetEntry(b);
  if (!first || !second || a == b || first->kind != EntryKind::kPinned ||
      second->kind != EntryKind::kPinned ||
      first->space_id != second->space_id) {
    return false;
  }
  if (first->split_partner == b) {
    return true;
  }
  BreakLink(a);
  BreakLink(b);
  FindEntry(a)->split_partner = b;
  FindEntry(b)->split_partner = a;
  FindEntry(b)->folder_id = first->folder_id;

  // `b` directly after `a`, which is where the sidebar draws the pair and so
  // where a reorder counting rows expects to find it.
  std::vector<EntryId> order;
  for (const TabEntry* sibling :
       EntriesForKind(first->space_id, EntryKind::kPinned)) {
    if (sibling->id != b) {
      order.push_back(sibling->id);
    }
  }
  order.insert(std::find(order.begin(), order.end(), a) + 1, b);
  for (size_t i = 0; i < order.size(); ++i) {
    FindEntry(order[i])->position = static_cast<int>(i);
  }
  Notify();
  return true;
}

void ArciumModel::UnlinkSplitEntry(EntryId id) {
  const TabEntry* entry = GetEntry(id);
  if (!entry || !entry->split_partner.is_valid()) {
    return;
  }
  BreakLink(id);
  Notify();
}

std::vector<EntryId> ArciumModel::PairOf(EntryId id) const {
  const TabEntry* entry = GetEntry(id);
  if (!entry) {
    return {};
  }
  const TabEntry* partner = GetEntry(entry->split_partner);
  if (!partner) {
    return {id};
  }
  return entry->position <= partner->position
             ? std::vector<EntryId>{id, partner->id}
             : std::vector<EntryId>{partner->id, id};
}

void ArciumModel::BreakLink(EntryId id) {
  TabEntry* entry = FindEntry(id);
  if (!entry) {
    return;
  }
  if (TabEntry* partner = FindEntry(entry->split_partner)) {
    partner->split_partner = EntryId();
  }
  entry->split_partner = EntryId();
}

void ArciumModel::DropBrokenSplitLinks() {
  for (TabEntry& entry : entries_) {
    if (!entry.split_partner.is_valid()) {
      continue;
    }
    const TabEntry* partner = GetEntry(entry.split_partner);
    const bool whole = partner && partner->id != entry.id &&
                       partner->split_partner == entry.id &&
                       partner->space_id == entry.space_id &&
                       partner->kind == EntryKind::kPinned &&
                       entry.kind == EntryKind::kPinned;
    if (!whole) {
      entry.split_partner = EntryId();
    }
  }
}

}  // namespace arcium
