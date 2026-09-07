// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/tab_binding.h"

namespace arcium {

TabBinding::TabBinding() = default;
TabBinding::~TabBinding() = default;

void TabBinding::Bind(EntryId id, tabs::TabHandle handle) {
  // Release whatever either side was previously bound to first, so the maps
  // never grow a second edge for the same entry or the same tab.
  UnbindEntry(id);
  UnbindTab(handle);
  entry_to_tab_[id] = handle;
  tab_to_entry_[handle] = id;
}

void TabBinding::UnbindEntry(EntryId id) {
  auto it = entry_to_tab_.find(id);
  if (it == entry_to_tab_.end()) {
    return;
  }
  tab_to_entry_.erase(it->second);
  entry_to_tab_.erase(it);
}

void TabBinding::UnbindTab(tabs::TabHandle handle) {
  auto it = tab_to_entry_.find(handle);
  if (it == tab_to_entry_.end()) {
    return;
  }
  entry_to_tab_.erase(it->second);
  tab_to_entry_.erase(it);
}

std::optional<tabs::TabHandle> TabBinding::TabForEntry(EntryId id) const {
  auto it = entry_to_tab_.find(id);
  if (it == entry_to_tab_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<EntryId> TabBinding::EntryForTab(tabs::TabHandle handle) const {
  auto it = tab_to_entry_.find(handle);
  if (it == tab_to_entry_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<EntryId> TabBinding::BoundEntries() const {
  std::vector<EntryId> ids;
  ids.reserve(entry_to_tab_.size());
  for (const auto& [id, handle] : entry_to_tab_) {
    ids.push_back(id);
  }
  return ids;
}

bool TabBinding::IsBound(tabs::TabHandle handle) const {
  return tab_to_entry_.contains(handle);
}

}  // namespace arcium
