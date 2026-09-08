// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "base/functional/callback.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
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
// and it has no way to reach the folder's name, depth or collapsed state
// otherwise.
struct SidebarFolder {
  FolderId id;
  // Absent at the top level. Never disagrees with `depth`: a folder reported
  // at depth 0 is reported with no parent, including one whose parent the
  // model could not find.
  std::optional<FolderId> parent_id;
  // 0 at the top level. What the list indents by, and -- because folders()
  // comes back in pre-order -- what lets a collapsed folder's subtree be
  // found as the run of folders after it with a greater depth.
  int depth = 0;
  std::u16string name;
  bool collapsed = false;
  // Entries in this folder's whole subtree, not just directly inside it. A
  // collapsed folder holding only subfolders would otherwise say "0" while
  // hiding everything under it.
  int entry_count = 0;
};

// One row of the archive list. Prepared by the model, like SidebarRow: the
// list derives nothing and knows nothing about SQLite.
struct ArchivedRow {
  GURL url;
  std::u16string title;
  // Half of the archive's primary key — `url` is the other half — which is
  // why both travel back with a reopen.
  base::Time archived_at;
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
  // Renames a row with no entry -- a Today tab. The name is not persisted:
  // a Today tab is transient and nothing carries a name past its life, so
  // this dies with the tab rather than outliving what it named. An empty
  // `title` clears it and the row follows the page again.
  //
  // Takes the strip index because that is what a row holds, and resolves it
  // to the tab's handle before returning: an index is only true at the
  // instant it is read. `expected_url` is the URL the row showed when the
  // edit opened; a tab that no longer matches it is a different page in the
  // same slot, and the rename is dropped rather than landing on it.
  virtual void SetTabTitle(int tab_index,
                           const GURL& expected_url,
                           const std::u16string& title) = 0;
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

  // How long a Today tab in the active space may sit idle before it is
  // archived. On the model rather than on the service because it is a
  // persistent property of the space, and the space bar's menu is the only
  // place it is chosen.
  virtual void SetArchiveTimeout(ArchiveTimeout timeout) = 0;
  virtual ArchiveTimeout archive_timeout() const = 0;

  // The archive, which unlike everything above is not in memory: it is a
  // SQLite file owned by a background sequence. So there is no
  // `archived_rows()` to match rows() — a synchronous getter over it could
  // only be a blocking read on the UI thread or a lie about freshness, and
  // Stage 6's library inherits whatever shape is set here.
  // `archive_readable` is false when the archive could not be read at all —
  // a file that would not open. It travels beside the rows because an empty
  // vector on its own is ambiguous, and the two states have to read
  // differently: "nothing archived yet" told to a user whose archive is
  // broken says their tabs were never archived, which is the opposite of the
  // truth and exactly the reassurance this feature exists to give.
  using ArchivedRowsCallback =
      base::OnceCallback<void(std::vector<ArchivedRow> rows,
                              bool archive_readable)>;

  // Whether this window has an archive at all. False off the record, where
  // there is no archive file and cannot be one — an off-the-record context's
  // GetPath() is the parent profile's, so an incognito archive would write
  // incognito browsing into the regular profile's file. See
  // ArciumProfileState::archive(). The affordance is then absent rather than
  // disabled: a control that can never be used reads as a bug.
  //
  // Deliberately *not* "the archive can be read": the file is opened on a
  // background sequence and this is asked while the sidebar is being built,
  // possibly before the open has run. A profile whose archive will not open
  // still answers true here and says so in the list instead — a button that
  // vanishes a few seconds after launch is worse than one that explains
  // itself.
  virtual bool has_archive() const = 0;

  // Asks for the `limit` most recently archived rows of the active space,
  // newest first. Always answers on a later turn of the run loop, never
  // inline, so a caller has one order of events to handle rather than two.
  //
  // `callback` may be dropped without ever running: the window can close
  // while the read is in flight. Bind it through a WeakPtr and put nothing in
  // it that has to happen.
  virtual void RequestArchivedRows(int limit,
                                   ArchivedRowsCallback callback) = 0;

  // Opens `url` in a new foreground tab and drops its archive row. An
  // archived tab was a Today tab and comes back as one, claimed by no entry.
  // Naming a row the archive no longer has still opens the tab and deletes
  // nothing, which is what a stale list clicked twice should do.
  virtual void ReopenArchived(const GURL& url, base::Time archived_at) = 0;

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
