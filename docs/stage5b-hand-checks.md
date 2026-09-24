# Stage 5b hand checks

Eight rows, walked against a running browser with this machine's own keyboard
and pointer. They recheck the Stage 5a rows that failed or asked for more
(1, 3, 4, 5, 7 and 8) and cover what this stage added.

Launch with `scripts/run`. Rows 7 and 8 need a second space.

| # | Do this | Look for |
|---|---|---|
| 1 | Drag a Today row towards the page and hold it near the right edge | While you drag, a strip appears at the page's right edge and brightens under the pointer. Letting go there puts the dragged page on the right |
| 2 | Drag the divider back and forth, then let go | The panes follow the pointer and stop when you stop, with no catching up afterwards |
| 3 | Look at the sidebar with a split of two Today tabs up | One row holding both pages side by side, left pane first, with only the focused half highlighted. There is no bracket |
| 4 | Split a pinned entry with a Today tab | The pair is one row in Pinned, where the pinned entry was |
| 5 | Pin one half of a split of two Today tabs, from its row menu | Both halves move to Pinned together as one row |
| 6 | Right-click either half of a pinned pair, then take End split | Two pinned rows, each on its own, and one page on screen |
| 7 | With a pinned pair and some Today tabs, quit with Cmd+Q and relaunch | The pair is one row in Pinned, the Today tabs are back, and no page appears twice. Clicking the pair shows both pages side by side |
| 8 | With the pair on screen, right-click a half, open Move to space and pick the other space | Both pages go together, and the window follows. In the new space they are one row and share the screen |

A row that fails is a finding: write down what you saw, not what should have
happened, and say which row it was.

## Result, 2026-09-23

Walked by the owner against the build holding 1e5d0ae, 666f923 and f9b06b6.
All eight rows passed.

- Row 2 passed for the divider itself, which follows the pointer smoothly,
  but the page behind it lags: on a heavy page the content can take up to ten
  seconds to settle at its new width. The divider and the resize are
  Chromium's own, unchanged by Arcium, so whether plain Chromium lags the
  same way is still to be checked with `--arcium-no-sidebar`. Open.
- Row 6, seen more widely: ending a split puts each half back where it came
  from, so two pinned halves stay pinned and a pinned half with a Today half
  go back to Pinned and Today. The owner judged that right.

Found outside the rows: a pinned row cannot be dragged to the very bottom of
the Pinned list; the lowest place it will land is one above the end. The
drop worked, but its line was drawn just past the list's edge and clipped,
so the lowest line on show was one row up. Fixed in 3fc6eb7 and confirmed by
the owner.

Row 2 follow-up: the sidebar rebuilt every row on each divider step; it no
longer does (791118f). The owner saw less lag but not none, and the same
drag in plain Chromium, through `--arcium-no-sidebar`, lags the same way. The
lag left is Chromium's own page resize, not Arcium's. Closed as upstream.

Added after the pass at the owner's request (73314f4): dropping a sidebar row
on the middle of another row splits the two. Rows 9-11 were walked by the
owner on 2026-09-23 and all three passed:

| # | Do this | Look for |
|---|---|---|
| 9 | Drag a Today row over the middle of another Today row, hold, then let go | While held there, that row's right half is tinted. Letting go splits the two, the dragged page on the right, drawn as one row |
| 10 | Drag a row near the top or bottom edge of another row | The line shows, no tint, and letting go moves the row as before |
| 11 | Drag a Today row over the middle of a pinned row that is not open | The pinned page opens on the left, the dragged page on the right |

Found while walking them: dragging the row whose page is already on screen
still offered the strip beside the page, which could only put a page beside
itself. The strip now asks, as the drag starts, whether the dragged row may go
beside the page, and does not appear when it may not (ea29ff9).

Also raised: a Today half could be dragged away to end its split, but a pinned
half could not, and a half dropped just below its own row kept its split,
because that drop moved nothing. At the owner's choice a split row now has a
handle between its halves. Dragging the handle moves the split whole and
keeps it; dragging either half pulls that half out and ends the split
wherever it lands, in Today, Pinned, a folder or Favourites. Rows 12-18 were
walked by the owner on 2026-09-24 and all seven passed:

| # | Do this | Look for |
|---|---|---|
| 12 | Point at a split row, then move away | A small six-dot handle appears in the gap between the halves, and goes when the pointer leaves the row |
| 13 | In Today, drag a split by its handle a few rows down and let go | Both pages move together, still one row side by side, landing where the line was |
| 14 | In Pinned, drag a split by its handle a few rows down and let go | The same: one row, both pages, still split, in the gap the line showed |
| 15 | In Today, drag one half of a split and let go just below its own row | The split ends: two separate rows, and one page on screen |
| 16 | In Pinned, drag one half of a split and let go just below its own row | The split ends and both stay pinned, as two rows |
| 17 | Drag a split by its handle over the middle of another row, then over Favourites | No tint on the row and no place to drop in Favourites: a split cannot join a third page or become one favourite |
| 18 | Drag a Today split by its handle into Pinned | It becomes one pinned row, still split |

Asked for after rows 12-18: the handle left a fixed gap between the halves.
They now sit together at rest and part only while the pointer is on the row
(0b283e4). Walked by the owner on 2026-09-24 and passed:

| # | Do this | Look for |
|---|---|---|
| 19 | Look at a split row, then point at it, then move away | At rest the halves nearly touch. Pointing slides them apart and the handle fades in; moving away fades it and slides them back. Neither outer edge of the row moves |
