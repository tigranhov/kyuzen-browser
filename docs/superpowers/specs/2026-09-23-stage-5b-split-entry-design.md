# Stage 5b: a split as one sidebar entry, and dropping a row beside the page

Approved by the owner on 2026-09-23, after the Stage 5a hand pass. Two
changes, both answers to what that pass found.

## 1. The band beside the page (built, 1e5d0ae)

Dragging a sidebar row onto the page did nothing on macOS: the page is a
native view that takes every drag crossing it, so a target laid over it is
never reached. While a row is being dragged the page now gives up an 80px
strip at its trailing edge and the drop target sits there. Dropping on it puts
the row's page beside the one on screen, on the right. Chromium's own target
for a dropped link is placed beside the page for the same reason.

## 2. A split is one entry

### What the reader sees

- A split shows as **one row**: the two halves side by side, each with its
  own icon and title, in pane order (left pane first). The half on screen
  with the focus draws the active background; the other half draws none.
- The row sits where the first of its two halves would have been. A Today tab
  split with a pinned entry is drawn in Pinned, beside that entry.
- The bracket and the two-pane mark go. The mark stays in one case: a split
  involving a favourite, which is a tile in the grid and never merges, so the
  other half still says it is sharing the screen.
- Clicking a half shows that page (and so the split). Each half's hover
  button closes that half. Rename renames that half.
- The row menu on either half adds **End split**, and its **Pin**, **Unpin**,
  **Move to space** and **Close both** act on the two pages together.
- **A pinned split is one pinned entry.** It survives a relaunch as one row,
  whether or not its tabs come back, and clicking it opens both pages side by
  side. Closing one of its tabs does not break it; ending the split does.
- A split can not be a favourite. Dragging either half into Favorites makes
  that half a favourite and breaks the pair, as it does today.

### Model

A pinned split is two pinned entries **linked** to each other:
`TabEntry::split_partner`, symmetric, persisted as `split_partner` in the
model file. Schema 6 to 7 with a no-op migration: an absent key is an entry
with no partner.

`ArciumModel` keeps the pair whole:

| Operation | On a linked entry |
|---|---|
| `LinkSplitEntries(a, b)` | Both must be pinned and in one space. Drops any earlier partner of either. Puts `b` in `a`'s folder, directly after `a`. |
| `UnlinkSplitEntry(a)` | Clears both sides. |
| `RemoveEntry(a)` | Clears the partner's side; the partner stays. |
| `ReorderEntry(a, p)` | Moves the pair, keeping its order. |
| `SetEntryKind(a, favourite)` | Unlinks first. |
| `SetEntryFolder(a, f)` | Moves the partner too. |
| `MoveEntryToSpace(a, s)` | Moves the partner too. |

A file whose link is one-sided, names a missing entry, or joins entries of two
spaces or two kinds loads with the link dropped, never refused.

### When pairs link and unlink

- A split forming between two pinned entries links them: a split of two pinned
  pages *is* a pinned split.
- Pinning either half of a split pins the other half too and links them. A
  Today tab split with a pinned entry is linked the same way when pinned.
- Unpinning either half, or dragging it into Today, unpins both.
- A split ending while both linked tabs are still open was ended by the
  reader, and unlinks them. A split ending because one tab closed leaves the
  link: the entry goes half cold, and clicking it reopens that half.
- Ending the split from inside Arcium to move the pair unlinks nothing.

### Moving a split to another space

Both pages move, the split is recorded for the target space, and it re-forms
there through the same path that re-forms a split after a relaunch. A pinned
pair stays linked, so it is one row in the new space either way.

### Rows

`SidebarRow` gains `split_partner` (the partner entry, for linked rows) and
`drawn_section` (where the row is drawn, when that is not its own section).
`GroupSplitRows` replaces `MarkSplitNeighbours`: it finds each pair by live
split or by link, emits the two rows next to each other in pane order at the
place of the earlier one, sets `split_joins_next` on the first and
`split_joins_previous` on the second, copies the first's drawn section and
folder to the second, and marks only the focused half active. Pairs involving
a favourite are left alone.

### Views

`TabListView` filters on the drawn section. A pair's second half is taken out
of the vertical layout and both halves are placed side by side in one row's
height. A drop can only land before or after a pair, never between its
halves, because both share one centre line.

### Out of scope

Three and four panes (D5-1). A favourite in a pair. Dragging a pair as one
object: dragging a half carries that half, and the model's pair rules make
Pinned and Today drops act on both.

## Performance

No process, no timer, no allocation at rest. Grouping is one pass over rows
already built. The link is one id per entry.
