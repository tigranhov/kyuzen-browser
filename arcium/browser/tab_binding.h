// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_TAB_BINDING_H_
#define ARCIUM_BROWSER_TAB_BINDING_H_

#include <map>
#include <optional>

#include "arcium/browser/model/entry_id.h"
#include "components/tabs/public/tab_interface.h"

namespace arcium {

// Joins a persistent entry to the tab currently representing it. A tab is
// held as tabs::TabHandle, which is already weak, so a closed tab leaves no
// dangling pointer and the entry simply reads as cold.
//
// Both directions are kept because both are asked: the sidebar asks "does
// this entry have a tab", and a tab-strip callback asks "which entry owns
// this tab". The invariant is one-to-one, enforced in Bind().
class TabBinding {
 public:
  TabBinding();
  TabBinding(const TabBinding&) = delete;
  TabBinding& operator=(const TabBinding&) = delete;
  ~TabBinding();

  // Releases whatever either side was previously bound to, so the map cannot
  // grow a second edge for the same entry or the same tab.
  void Bind(EntryId id, tabs::TabHandle handle);
  void UnbindEntry(EntryId id);
  void UnbindTab(tabs::TabHandle handle);

  std::optional<tabs::TabHandle> TabForEntry(EntryId id) const;
  std::optional<EntryId> EntryForTab(tabs::TabHandle handle) const;
  bool IsBound(tabs::TabHandle handle) const;

 private:
  std::map<EntryId, tabs::TabHandle> entry_to_tab_;
  std::map<tabs::TabHandle, EntryId> tab_to_entry_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_TAB_BINDING_H_
