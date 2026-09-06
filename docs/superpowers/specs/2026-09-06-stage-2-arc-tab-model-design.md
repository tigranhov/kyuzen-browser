# Stage 2: The Arc Tab Model — Design

Date: 2026-09-06
Status: Draft, awaiting review
Parent: `2026-09-05-arcium-browser-design.md`, section 5 Stage 2 (R2.1 to R2.7), sections 4.2 and 4.5
Inputs: `docs/stage1-findings.md`; the session-restore spike recorded in section 8 below

## 1. Goal

Favorites, Pinned and Today stop being a view over Chromium's tab strip and become an Arcium-owned
model that outlives tabs and survives restarts. One space still, with the schema ready for many.

## 2. What forces the change

Stage 1's `SidebarRow` is a pure function of `TabStripModel`, keyed by tab index. Three Stage 2
requirements break that outright:

- A favourite exists with no live tab (R2.1). Index cannot name it.
- A pinned tab persists across a restart before any tab is created for it (R2.2).
- An archived tab has no tab at all (R2.3).

So Arcium needs its own entities with stable identity, its own persistence, and a binding between
an entity and the live tab that currently represents it.

## 3. Where the truth lives

Decided: **a side table over the tab strip.** Arcium owns persistent entries; `TabStripModel`
remains the source of truth for live tabs; the sidebar model merges the two. This is CLAUDE.md's
rule for Spaces applied a stage early, and it never contests tab creation with Chromium.

Rejected, with reasons:

- **Arcium as the source of truth**, tab strip as a materialisation cache. Uniform for the views,
  but every tab born outside Arcium — `target=_blank`, an extension, session restore, Cmd+N — must
  be adopted, and any gap is a divergence bug. Too much surface for the gain.
- **Chromium's pinned tabs plus the bookmark model.** Least new code, but Chromium pinned tabs are
  always live, so lazy pinning is impossible, and archive, folders and rename do not fit the
  bookmark model. A dead end for R2.3.

## 4. Components

### 4.1 `arcium/browser/model/` — no Chromium UI dependencies, unit-testable without a browser

| Type | Responsibility |
|---|---|
| `EntryId` | Stable identity. A `base::Uuid` string in JSON, an opaque value in memory |
| `TabEntry` | `id, kind {kFavorite, kPinned}, space_id, folder_id, position, url, custom_title, last_title, created_at`. `url` is the home URL for a favourite and the pinned URL for a pinned tab |
| `Folder` | `id, space_id, name, collapsed, position` |
| `Space` | `id, name, archive_timeout`. One default space this stage |
| `ArciumModel` | Owns spaces, entries and folders. Mutations plus an observer list. Knows nothing about tabs |

`ArciumModel` is deliberately ignorant of live tabs. Everything in it is data that survives a quit,
which is what makes it testable without a browser.

### 4.2 `arcium/browser/` — persistence

`ModelStore` serialises `ArciumModel` to one JSON file per Chromium profile through
`ImportantFileWriter`, on a background sequence, debounced so a drag that moves ten rows writes
once. Reads at startup on a background sequence.

`ArchiveStore` is SQLite through `sql::Database` on its own background sequence: one table of
`url, title, space_id, archived_at`, indexed on `archived_at` and on `space_id`. Inserted on
archive, queried by the archive list and by search.

Schema versioning from the first commit in both stores, because Stage 3 adds spaces and Stage 6
adds the library.

### 4.3 `arcium/browser/` — binding

`TabBinding` maps `EntryId` to a live tab and back. A live tab is held as `tabs::TabHandle`, which
is already weak, so a closed tab unbinds without a dangling pointer.

The three states a sidebar row can be in:

| Row | Entry | Live tab |
|---|---|---|
| Cold favourite or pinned entry | yes | no |
| Warm favourite or pinned entry | yes | yes |
| Today tab | no | yes |

Clicking a cold entry opens its `url` and binds. Closing a warm entry's tab unbinds and leaves the
entry. Closing a Today tab is just a tab closing.

### 4.4 `arcium/ui/browser/` — the merge

`SidebarTabModel` grows from "derive rows from the strip" into "merge entries with live tabs".
`SidebarRow` gains an `EntryId` and keeps its tab index, with either able to be absent. The views
from Stage 1 change only where they address a row by index today.

### 4.5 Drag and drop

`views::DragController` on `TabRowView` and the favourites tiles. The drag payload is a row
identity — an entry id, or a tab handle for a Today tab. Drop targets: the favourites grid, the
pinned list, a folder header, and the Today list. A drop that changes a row's section is a model
mutation, not a tab-strip move; a drop that reorders within Today is a tab-strip move, as today.

### 4.6 Row interactions

**Rename (R2.4).** Double-click or a context-menu item turns the row's label into a `views::Textfield`
in place. Enter commits to `TabEntry::custom_title`, Esc abandons. A custom title wins over the
page title for ever after, including when the tab reloads; clearing the field restores the page
title. Today tabs cannot be renamed, since they own no entry to carry the name.

**Return to pinned URL (R2.2).** A warm pinned entry whose tab has navigated away from `url` shows
a revert affordance on hover, with a keyboard shortcut. It navigates the bound tab back rather than
opening a new one.

**Folders (R2.5).** Create from the pinned list's context menu, rename in place by the same
mechanism as tab rename, collapse by clicking the header, populate by dragging pinned rows in and
out. Folders hold pinned entries only.

Master spec 4.2 suggests building folders "on Chromium's tab group data where it fits". It does not
fit: a Chromium tab group holds live tabs, and a cold pinned entry has none. Folders are therefore
purely Arcium entities. Chromium tab groups remain available and untouched for Today tabs.

**Clear (R2.3).** The divider's Clear action archives every Today tab in the space at once, obeying
the same never-archive rules as the idle timeout.

### 4.7 Search (R2.6)

`TabSearchService` queries three sources — live tabs, entries, and the archive — and returns ranked
results over title and URL. Unit-tested, with no UI this stage; Stage 4's command bar becomes its
front end. This avoids building a search surface twice.

### 4.8 Archive UI

A deliberately plain list: archived entries newest first, click to reopen, reachable from the
divider. Enough to execute A2.2 and to trust auto-archive in daily use. Stage 6's library replaces
it. It is scoped as throwaway and should not grow features.

## 5. Auto-archive without polling (R2.3)

Each live Today tab carries a last-active time. Rather than a timer per tab or any polling, one
one-shot timer per browser is scheduled for the earliest upcoming expiry and recomputed whenever
activity moves it. Archiving a tab writes an archive row and closes the tab.

Tabs restored after a restart take the model's last-save time as their last-active floor, so a
browser closed overnight archives yesterday's Today tabs on launch. That is both the behaviour Arc
has and the reason no per-tab timestamp needs to survive the quit.

Never archived: the active tab, a tab playing audio, and a tab with an unsubmitted form or a
`beforeunload` handler — closing those is data loss, not tidying.

## 6. Startup must not block first paint

The JSON read and the SQLite open both happen on background sequences. The sidebar draws live tabs
as soon as the window exists, exactly as in Stage 1; entries appear when the read completes. No
Arcium disk I/O is on the critical path to first paint.

## 7. Session restore and rebinding

Chromium's session restore brings tabs back with no memory of which Arcium entry they belonged to.
The spike in section 8 settles how they are rejoined:

1. Arcium writes each bound tab's `EntryId` into Chromium's per-tab session `extra_data` through
   `SessionService::AddTabExtraData`.
2. On restore the id arrives in `SessionTab::extra_data` and reaches `chrome::AddRestoredTab`,
   where a hook hands it to Arcium, which binds the tab to the entry.
3. Because a session-command rebuild drops previously written extra data (section 8, finding 3), a
   second hook in `SessionService::BuildCommandsForTab` re-emits the key on every rebuild.

An entry whose tab was not restored is simply cold. A restored tab with no Arcium key is a Today
tab. Both are correct outcomes, so a lost key degrades to cold rather than to corruption.

## 8. Spike: is per-tab identity stable across a restart? (2026-09-06)

Answered against the pinned checkout, Chromium 152.0.7977.83.

1. **`SessionID` is not stable across a restart.** `SessionTabHelper` assigns
   `SessionID::NewUnique()` in its constructor unconditionally
   (`components/sessions/content/session_tab_helper.cc:23`), and no restore path reassigns the
   persisted id. `SessionIdGenerator::SetHighestRestoredID`, the API that exists precisely to let
   restored ids survive, has no production caller in the tree — only unit tests. Keying Arcium
   entries on `SessionID` is therefore impossible.

2. **Per-tab `extra_data` is the supported mechanism.**
   `SessionService::AddTabExtraData(window_id, tab_id, key, data)` persists an arbitrary string per
   tab into Chromium's session file. It returns as `SessionTab::extra_data`, flows through
   `SessionRestore` into `chrome::AddRestoredTab`, and is consumed there by two upstream features
   in one line each — glic and send-tab-to-self (`chrome/browser/ui/browser_tabrestore.cc:88`).
   Arcium's hook is a third such line, which is as clean a seam as this codebase offers.

3. **A command rebuild drops extra data.** `SessionService::ScheduleResetCommands()` rebuilds the
   command list from live browser state, and neither `session_service.cc` nor
   `session_service_base.cc` re-emits tab extra data while doing so. Upstream's two users tolerate
   the loss because their bit is benign; for Arcium it would silently unbind a pinned tab. Hence
   the `BuildCommandsForTab` hook in section 7, which is already where per-tab commands are
   appended on a rebuild.

## 9. Hook patches

| Patch | Seam | Delegates to |
|---|---|---|
| `0110` | `chrome::AddRestoredTab` in `browser_tabrestore.cc` | `arcium::RestoreTabEntryFromExtraData` |
| `0120` | `SessionService::BuildCommandsForTab` | `arcium::AppendTabEntryCommand` |

Both are a few lines beside existing upstream calls of the same shape.

## 10. Performance

The four questions from CLAUDE.md, answered before building:

1. **A new process, or one kept alive longer?** No. The model, both stores' clients and the search
   service live in the browser process. SQLite runs on a sequence, not a process.
2. **Idle memory per window or per tab?** One model per Chromium profile, shared by every window;
   entries are small structs. Per tab it is one handle in the binding table. Lazy pinned entries
   make this stage a memory *reduction* against restoring every pinned tab as a live tab.
3. **Work before first paint?** None. Section 6.
4. **UI-thread work not needed for the frame?** No. Writes are debounced onto a background
   sequence, SQLite is off-thread, and auto-archive is one timer per browser rather than polling.

## 11. Testing

Unit tests, in `arcium/test/`, needing no browser:

- Model mutations: pin, favourite, rename, create and collapse a folder, reorder, move between
  sections.
- JSON round-trip, including an unknown future field and a truncated file.
- Archive SQL: insert, query by recency, query by space, schema upgrade from version 1.
- Idle computation: the next-expiry calculation, the restart floor, and each never-archive rule.
- Search ranking across the three sources.

Browser-level verification stays as Stage 1 established it — the checklist in
`docs/stage2-findings.md`, executed over the DevTools protocol — because building Chromium's
browser-test target costs hours on this machine. A2.1's quit-and-relaunch is executed by hand.

## 12. Risks

| Risk | Mitigation |
|---|---|
| Session `extra_data` proves lossier than the spike suggests | Degrades to a cold entry, never to corruption. The quit-and-relaunch check in A2.1 exercises it directly |
| Drag and drop is the largest new UI surface and Views drag is fiddly | Build it in the playground first, as Stage 1 did for the sidebar |
| Two persistence stores land in one stage | JSON first with the model behind it; SQLite only serves archive and search, so it can land second without blocking |
| The archive list grows features and becomes Stage 6 by accident | Scoped as throwaway in 4.8; reopen and list, nothing else |
