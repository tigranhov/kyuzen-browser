// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/model/entry_id.h"
#include "base/observer_list_types.h"
#include "ui/base/models/image_model.h"
#include "url/gurl.h"

namespace arcium {

enum class SidebarSection { kFavorites, kPinned, kToday };

// Everything a row needs to paint itself. Derived by the model, never by views.
struct SidebarRow {
  int tab_index = -1;
  SidebarSection section = SidebarSection::kToday;
  std::u16string title;
  ui::ImageModel favicon;
  bool is_active = false;
  bool is_loading = false;
  bool is_audible = false;
  bool is_muted = false;
  GURL url;
  // Set when the row is backed by a persistent entry. Invalid for a Today
  // tab, which has no entry.
  EntryId entry_id;
  // An entry with no live tab. It draws from last_title and the entry's URL,
  // and clicking it opens that URL.
  bool is_cold = false;
  // A warm pinned entry whose tab has navigated away from the pinned URL.
  bool can_return_to_pinned_url = false;
  // Set on rows inside a folder, so TabListView can indent and hide them.
  std::optional<FolderId> folder_id;
};

// The sidebar's view of a window's tabs plus the commands it can issue. The
// browser implements it on top of TabStripModel; the playground uses a fake.
class SidebarModel {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Fired after any change. Views re-read rows() and rebuild; the model
    // coalesces bursts so one tab-strip event yields one notification.
    virtual void OnSidebarModelChanged() = 0;
  };

  virtual ~SidebarModel() = default;

  virtual std::vector<SidebarRow> rows() const = 0;

  virtual void ActivateTab(int tab_index) = 0;
  virtual void CloseTab(int tab_index) = 0;
  virtual void MoveTab(int from_index, int to_index) = 0;
  virtual void NewTab() = 0;
  virtual void ClearToday() = 0;

  // Entry commands. A row backed by an entry routes through these instead of
  // the tab-index commands above: the entry outlives the tab, so identity has
  // to be the entry's, not a position in the strip.
  virtual void AddToFavorites(int tab_index) = 0;
  virtual void PinTab(int tab_index) = 0;
  // Drops the entry. Its tab, if any, falls back into Today.
  virtual void UnpinEntry(EntryId id) = 0;
  // Focuses the entry's tab, or opens the entry's URL when it is cold.
  virtual void ActivateEntry(EntryId id) = 0;
  // Closes the entry's tab but keeps the entry, which turns cold.
  virtual void CloseEntryTab(EntryId id) = 0;
  virtual void SetEntryTitle(EntryId id, const std::u16string& title) = 0;
  // Navigates the entry's bound tab back to the entry's URL.
  virtual void ReturnToPinnedUrl(EntryId id) = 0;

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
