# Stage 5b: a split as one sidebar entry

Design: `docs/superpowers/specs/2026-09-23-stage-5b-split-entry-design.md`.
Plan: `docs/superpowers/plans/2026-09-23-stage-5b-split-entry.md`. Hand rows:
`docs/stage5b-hand-checks.md`.

## Where this stage stands

**Built and green, hand rows not walked, perf not measured.** Both changes came
from the Stage 5a hand pass on 2026-09-23. 773 unit tests pass. All 11 split
browser tests pass, and so do the 40 restore, profile, space-move and box
command browser tests that the changes reach. The full browser suite has not
been run in one sitting since these changes.

## What was built

**Dropping a row beside the page** (`1e5d0ae`). On macOS the page takes every
drag that crosses it, so a target laid over the page never saw a row dropped
on it, which is why row 1 of the 5a pass did nothing. While a row is dragged
the page now gives up an 80px strip at its trailing edge, and the drop target
sits there. Chromium's own target for a dropped link sits beside the page for
the same reason.

**A split is one row** (`f9b06b6`). The two halves are drawn side by side in
one row, in pane order, and only the half with the focus reads as current. A
Today tab split with a pinned entry is drawn beside it in Pinned. The bracket
is gone. The two-pane mark remains only on a split with a favourite, because a
favourite is a tile and never merges with a row.

**A pinned split is one entry.** Two pinned entries sharing the screen are
linked in the model, and model schema 6 becomes 7 with a no-op migration. A
linked pair reorders, goes into folders and moves between spaces together,
and it comes back as one row after a relaunch whether or not its tabs do.
Clicking either half of a cold pair opens both pages side by side. Pinning
either half of a split pins both, and unpinning or dragging either half to
Today unpins both. Ending the split unlinks the pair, but closing one of its
tabs does not. The row menu gains "End split" and "Close both".

## A defect found on the way, older than this stage

**A pinned tab came back cold after a relaunch, with its page beside it a
second time as a Today tab** (`666f923`). Session restore does not wait for
the model file, which is read on another sequence. A restored tab therefore
asked for its entry before the model held it, and the old code treated that
the same as an entry that had been deleted. The new relaunch browser test
found it, and a single pinned tab showed the same result, so it is not
specific to splits. Logging confirmed the cause: at bind time the model had
not loaded and did not hold the entry. The profile state now holds such a
bind and applies it when the load finishes. An entry still missing after the
load leaves the tab in Today, as before. This is most likely what row 7 of
the 5a pass saw. Stage 2's hand pass had seen pins come back warm, which
suggests the load used to win that race and no longer does.

## Tests

- `split_entry_model_unittest.cc`, 13 tests: linking, the pair rules for
  reorder, folder, space, removal and kind change, and the file round trip,
  including links that load dropped rather than refused.
- `split_entry_unittest.cc`, 9 tests against a real tab strip: pin both,
  link on split, unlink on end, keep the link when one or both tabs close,
  unpin both, open a cold pair side by side, and a Today tab drawn in Pinned.
- `split_rows_unittest.cc` was rewritten for grouping (8 pure tests plus 3
  strip-backed ones).
- `sidebar_views_unittest.cc` gained two tests: the halves share one row's
  height, and the menu on a half offers End split and Close both.
- `session_tab_entry_unittest.cc` gained a test for a bind that arrives
  before the model loads.
- The browser test `SplitViewTest.APinnedSplitComesBackAsOneEntry` relaunches
  a pinned pair and checks it is linked, drawn once, and sharing the screen.

Watched to fail with the code deliberately broken: disabling the pin rule
failed the pin test, and disabling the unlink and cold-pair rules failed
those two tests. The held bind's unit test failed with the hold removed, and
the browser relaunch test failed on the real defect before the fix, drawing
each page twice.

## Known limits

- A pair moved to another space from a menu, while neither half is on
  screen, arrives as one row and is not re-formed until clicked.
- A Today-only pair moved across profiles may not re-form on arrival,
  because the reopen replaces the tab it was going to split with. It still
  arrives in the right space.
- Dragging a pair carries the half you picked up. Drops into Pinned and Today
  act on both halves through the model's pair rules.

## Owed

- The hand rows in `docs/stage5b-hand-checks.md`.
- Perf, and a whole-suite browser run.
