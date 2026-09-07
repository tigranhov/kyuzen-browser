// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/tab_binding.h"

#include <utility>

namespace arcium {

TabBinding::TabBinding() = default;
TabBinding::~TabBinding() = default;

void TabBinding::Bind(EntryId id, tabs::TabHandle handle) {
  // Rebinding the edge that is already there changes nothing, and the change
  // callback costs a posted rebuild of the whole session command list. The
  // one-to-one invariant means this one lookup settles both maps.
  auto existing = entry_to_tab_.find(id);
  if (existing != entry_to_tab_.end() && existing->second == handle) {
    return;
  }
  // Release whatever either side was previously bound to first, so the maps
  // never grow a second edge for the same entry or the same tab.
  EraseEntry(id);
  EraseTab(handle);
  entry_to_tab_[id] = handle;
  tab_to_entry_[handle] = id;
  NotifyChanged();
}

void TabBinding::UnbindEntry(EntryId id) {
  if (EraseEntry(id)) {
    NotifyChanged();
  }
}

void TabBinding::UnbindTab(tabs::TabHandle handle) {
  if (EraseTab(handle)) {
    NotifyChanged();
  }
}

void TabBinding::SetChangedCallback(base::RepeatingClosure callback) {
  changed_callback_ = std::move(callback);
}

void TabBinding::NotifyChanged() {
  if (suppress_depth_ == 0 && changed_callback_) {
    changed_callback_.Run();
  }
}

TabBinding::ScopedChangeSuppression::ScopedChangeSuppression(
    TabBinding* binding)
    : binding_(binding) {
  ++binding_->suppress_depth_;
}

TabBinding::ScopedChangeSuppression::~ScopedChangeSuppression() {
  --binding_->suppress_depth_;
}

bool TabBinding::EraseEntry(EntryId id) {
  auto it = entry_to_tab_.find(id);
  if (it == entry_to_tab_.end()) {
    return false;
  }
  tab_to_entry_.erase(it->second);
  entry_to_tab_.erase(it);
  return true;
}

bool TabBinding::EraseTab(tabs::TabHandle handle) {
  auto it = tab_to_entry_.find(handle);
  if (it == tab_to_entry_.end()) {
    return false;
  }
  entry_to_tab_.erase(it->second);
  tab_to_entry_.erase(it);
  return true;
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
