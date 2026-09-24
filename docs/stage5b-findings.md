# Stage 5b: a split as one sidebar entry

Design: `docs/superpowers/specs/2026-09-23-stage-5b-split-entry-design.md`.
Plan: `docs/superpowers/plans/2026-09-23-stage-5b-split-entry.md`. Hand rows:
`docs/stage5b-hand-checks.md`.

## Where this stage stands

**Built and green, all eight hand rows passed on 2026-09-23, perf not measured.** Both changes came
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
- A split row can be dragged two ways. Its handle, between the halves, carries
  the pair, which moves whole and stays split. Either half carries only
  itself, and dropping it anywhere ends the split, even right below its own
  row. A pinned pair dropped in a folder by its handle goes in whole by the
  model's pair rule; the fake model the view tests use does not have that
  rule, so only the half case is tested there.

## Open

- The page behind the split divider lags on heavy pages, up to ten seconds
  to settle, while the divider itself is smooth. Part of it was the sidebar
  rebuilding on every divider step, fixed in 791118f. The rest is Chromium's:
  plain Chromium lags the same way.

## Fixed and added after the hand pass

- A pinned row could not be dropped at the very bottom of Pinned: the line
  for that drop was drawn past the list's edge and clipped (3fc6eb7).
- Dropping a row on the middle of another row splits the two, at the
  owner's request (73314f4). Hand rows 9 to 11 passed.
- The strip beside the page was offered when dragging the row whose page was
  already on screen, which could only put a page beside itself. It now shows
  only for a row that may go there (ea29ff9).
- A split row has a handle between its halves, at the owner's choice, so a
  split can be both moved whole and ended by dragging a half away, the same in
  Today and Pinned. Before, a Today half could be dragged out and a pinned
  half could not, and a half dropped just below its own row kept its split.
  Hand rows 12 to 18 passed on 2026-09-24. Fifteen tests were written for it, and
  thirteen were watched to fail with the piece they cover broken. The test
  that a pair is never offered as a split target still passed with its guard
  removed, because the model already refuses to split a tab that is split;
  the guard stays as the view's own statement of the rule. The two tests
  that nothing happens for a row sharing nothing were not broken on purpose.
- The handle's room is opened only while the pointer is on the row, at the
  owner's request (0b283e4): at rest the halves sit 2px apart, and they slide
  to 12px over 150ms as the handle fades in, then back when the pointer
  leaves; reduced motion switches at once. Hand row 19 covers it.

## Owed

- Perf.

Both whole suites ran on 2026-09-24 with the owner's approval: 800 unit
tests and 93 browser tests, all passed. One browser test, a split coming back
in a space that is not on screen, ran past the launcher's 45-second limit on
its first try while the machine's load average was near 400, then passed in
6 seconds on the retry and three more times on its own.
