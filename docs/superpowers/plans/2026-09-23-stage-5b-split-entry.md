# Stage 5b: a split as one entry — implementation plan

**Spec:** docs/superpowers/specs/2026-09-23-stage-5b-split-entry-design.md

**Goal:** a split draws as one sidebar row, and a pinned split is one pinned
entry that survives a relaunch.

**Global constraints:** all code in `arcium/`, no new patch; files under ~500
lines; `scripts/format`; a failing test before each fix; no process, timer or
idle allocation added.

## Task 1: the link in the model

Files: `browser/model/tab_entry.h`, `arcium_model.{h,cc}`,
`model_serializer.{h,cc}`, `model_migration.cc`; tests in
`arcium_model_unittest.cc`, `model_serializer_unittest.cc`,
`model_migration_unittest.cc`.

- `TabEntry::split_partner` (EntryId).
- `ArciumModel::LinkSplitEntries(EntryId a, EntryId b) -> bool`,
  `UnlinkSplitEntry(EntryId)`; the pair rules of the spec's table in
  `RemoveEntry`, `ReorderEntry`, `SetEntryKind`, `SetEntryFolder`,
  `MoveEntryToSpace`.
- Serialiser writes `split_partner`; loader keeps only mutual, same-space,
  both-pinned links. `kModelSchemaVersion` 7, no-op step 6 to 7.

Tests: linking places the partner after and in the same folder; a favourite
or cross-space pair is refused; reorder moves both in order; folder and space
moves carry the partner; removing one clears the other's link; round trip;
a one-sided link loads dropped; a version 6 file migrates with no partners.

## Task 2: grouping rows

Files: `ui/sidebar/sidebar_model.h`, `ui/sidebar/split_rows.{h,cc}`,
`ui/browser/sidebar_tab_model.cc`, `sidebar_tab_model_entries.cc`; tests in
`split_rows_unittest.cc` (rewritten).

- `SidebarRow::split_partner`, `SidebarRow::drawn_section` plus
  `DrawnSection()`.
- `GroupSplitRows(std::vector<SidebarRow>&, int active_tab_index)` replaces
  `MarkSplitNeighbours`, as the spec describes.
- `RowForEntry` fills `split_partner`.

Tests: two Today halves apart in the list come out adjacent in pane order;
a pinned half and a Today half are drawn in Pinned; a cold linked pair
groups with no live split; only the focused half is active; a favourite's
split is left alone with its mark; a lone half is untouched.

## Task 3: drawing a pair

Files: `ui/sidebar/tab_list_view.{h,cc}`, `tab_row_view.cc`,
`sidebar_view.cc` if it filters rows; tests in `sidebar_views_unittest.cc`.

- Lists filter on `DrawnSection()`.
- The second half is ignored by layout; `Layout` puts the halves side by
  side in one row's height. The bracket goes.
- A merged half carries no split mark.

Tests: a pair occupies one row's height, the halves share a y and split the
width; the row after a pair sits one row below it.

## Task 4: acting on a pair

Files: `sidebar_model.h`, `sidebar_tab_model*.cc`, `split_controller.{h,cc}`,
`row_context_menu.{h,cc}`, the playground's fake model; tests in
`split_controller_unittest.cc`, a new `split_entry_unittest.cc`,
`sidebar_views_unittest.cc` (menu).

- `SidebarModel::EndSplit(row)`, `CloseSplit(row)`.
- Pair-aware `PinTab`, `MoveTabToSection`, `UnpinEntry`,
  `MoveEntryToSection`, `MoveEntryToFolder`, `ActivateEntry` (a cold pair
  opens both and splits), `MoveTabToSpace` and `MoveEntryToSpace` (both move,
  the split is recorded for the target space and re-formed).
- Link on a split forming between two pinned entries; unlink when a split ends
  with both linked tabs still open, unless Arcium ended it to move the pair.
- Menu: "End split" on a merged half; "Close" becomes "Close both" there.

Tests: pinning a Today half pins and links both; unpinning one unpins both;
splitting two pinned entries links them; ending the split unlinks; closing
one half keeps the link; clicking a cold pair opens both side by side;
moving a pair to another space moves both and keeps the link.

## Task 5: through the browser, and the record

- Browser test: a pinned pair quits and comes back as one row and as a split.
- Hand rows for the owner in `docs/stage5b-hand-checks.md`.
- Findings, CLAUDE.md row, perf.
