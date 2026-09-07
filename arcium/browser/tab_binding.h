// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_TAB_BINDING_H_
#define ARCIUM_BROWSER_TAB_BINDING_H_

#include <map>
#include <optional>
#include <vector>

#include "arcium/browser/model/entry_id.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
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

  // Fired after every change to either map. This is the choke point for
  // "the set of warm entries just changed", and the one thing that has to
  // happen then — asking Chromium to rewrite the session file so the change
  // survives a quit — needs //chrome, which this target must not reach. So
  // the closure is installed from arcium/ui/browser instead. See
  // InstallSessionRebuildNudge.
  void SetChangedCallback(base::RepeatingClosure callback);

  // Silences that callback for its lifetime, for the one caller that knows
  // the session file is about to be rewritten anyway. Session restore binds
  // every warm entry in a row, during startup, and Chromium rebuilds the
  // command list when the restore finishes; asking for another rebuild per
  // restored pin would be startup work for nothing.
  class ScopedChangeSuppression {
   public:
    explicit ScopedChangeSuppression(TabBinding* binding);
    ScopedChangeSuppression(const ScopedChangeSuppression&) = delete;
    ScopedChangeSuppression& operator=(const ScopedChangeSuppression&) = delete;
    ~ScopedChangeSuppression();

   private:
    const raw_ptr<TabBinding> binding_;
  };

  // Every entry that currently holds a binding. The caller that owns both
  // this and the model uses it to drop bindings whose entry has gone away:
  // ArciumModel::ReplaceAll removes entries without touching the binding.
  std::vector<EntryId> BoundEntries() const;

 private:
  // Erase without notifying, so Bind — which releases both sides first —
  // reports one change rather than three. Return whether anything went.
  bool EraseEntry(EntryId id);
  bool EraseTab(tabs::TabHandle handle);
  void NotifyChanged();

  std::map<EntryId, tabs::TabHandle> entry_to_tab_;
  std::map<tabs::TabHandle, EntryId> tab_to_entry_;
  base::RepeatingClosure changed_callback_;
  int suppress_depth_ = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_TAB_BINDING_H_
