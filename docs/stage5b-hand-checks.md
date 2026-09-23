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
longer does (791118f). The owner saw less lag but not none, so the rest is
being compared against plain Chromium through `--arcium-no-sidebar`.
