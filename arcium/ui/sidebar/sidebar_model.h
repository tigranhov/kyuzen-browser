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

// Everything a folder header needs to paint itself. Like SidebarRow, it is
// prepared by the model: a header must never scan the rows to draw its count,
// and it has no way to reach the folder's name or collapsed state otherwise.
struct SidebarFolder {
  FolderId id;
  std::u16string name;
  bool collapsed = false;
  int entry_count = 0;
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
  // Both append, which is what a menu item means by them.
  virtual void AddToFavorites(int tab_index) = 0;
  virtual void PinTab(int tab_index) = 0;
  // Makes an entry for the tab at `tab_index` and puts it at `position` among
  // `section`'s entries. What a drop does, and the reason it is not the two
  // commands above: a drop drew an insertion indicator at a place before it
  // was taken, and appending would make that indicator a lie. A position past
  // the end of the section appends, so AddToFavorites and PinTab are this
  // command with no position asked for. kToday is a no-op — the tab is
  // already there, and the drop that would mean it is MoveTab.
  virtual void MoveTabToSection(int tab_index,
                                SidebarSection section,
                                int position) = 0;
  // Drops the entry. Its tab, if any, falls back into Today.
  virtual void UnpinEntry(EntryId id) = 0;
  // Focuses the entry's tab, or opens the entry's URL when it is cold.
  virtual void ActivateEntry(EntryId id) = 0;
  // Closes the entry's tab but keeps the entry, which turns cold.
  virtual void CloseEntryTab(EntryId id) = 0;
  virtual void SetEntryTitle(EntryId id, const std::u16string& title) = 0;
  // Navigates the entry's bound tab back to the entry's URL.
  virtual void ReturnToPinnedUrl(EntryId id) = 0;

  // Puts `id` in `section` at `position` among that section's entries. What a
  // drop does, and one command rather than a kind change followed by a
  // reorder: a drop changes both at once, so two calls would let an observer
  // see the entry in its new section still holding its old position, and a
  // failure between them would strand it mid-move.
  //
  // `position` is where the entry ends up once it has been lifted out of
  // wherever it was, not a gap in the section as it stands. The two readings
  // differ by one whenever an entry moves *down* inside its own section, and
  // it is the caller — the view that drew the insertion indicator — that
  // knows which of its rows was being dragged, so the conversion belongs
  // there.
  //
  // kToday drops the entry rather than moving it — Today is tabs, and a tab
  // is not an entry — but never the page: a warm entry's tab stays behind,
  // and a cold entry's URL is opened as a tab first, so nothing is lost and
  // the drop needs no undo. `position` still counts, because Today's order is
  // the tab strip's: the tab left behind moves to the `position`-th place
  // among Today's rows, which is what the insertion line promised.
  //
  // Naming an id the model does not have, or a section it cannot reach, is a
  // no-op rather than a crash, because the drag that issued this began from a
  // snapshot of rows().
  virtual void MoveEntryToSection(EntryId id,
                                  SidebarSection section,
                                  int position) = 0;

  // Folder commands. Folders hold pinned entries only, so every one of these
  // that names a favourite, a Today row or an id the model no longer has is a
  // no-op rather than a crash: menus are built from a snapshot of rows() and
  // the model can move underneath them while the menu is open.
  //
  // The default space's folders in `position` order, each with the number of
  // entries inside it already counted.
  virtual std::vector<SidebarFolder> folders() const = 0;
  virtual void SetFolderCollapsed(FolderId id, bool collapsed) = 0;
  // Makes a folder holding just `id`. Returns an invalid id if it could not.
  virtual FolderId CreateFolderWithEntry(EntryId id,
                                         const std::u16string& name) = 0;
  // std::nullopt returns the entry to the top level of the Pinned section.
  virtual void MoveEntryToFolder(EntryId id,
                                 std::optional<FolderId> folder_id) = 0;
  virtual void SetFolderName(FolderId id, const std::u16string& name) = 0;
  // Removes the folder. Its entries return to the top level; a folder groups
  // entries, it does not own them.
  virtual void DeleteFolder(FolderId id) = 0;

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
