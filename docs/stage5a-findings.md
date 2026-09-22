# Stage 5a: two pages on one screen

R5.1. Design: `docs/superpowers/specs/2026-09-22-stage-5a-split-view-design.md`.
Plan: `docs/superpowers/plans/2026-09-22-stage-5a-split-view.md`. Hand rows:
`docs/stage5a-hand-checks.md`.

## Where this stage stands

**Written, not built.** Every line of it was written against headers and
implementations read in the Chromium checkout, in one pass, with no compile
and no test run — the machine was in use for the whole of it and the owner
asked that nothing be run until they said otherwise. Nothing here is verified.
The count of tests below is a count of tests written; none has been watched to
fail, and none has been watched to pass. Read every claim in this file as "the
code says", not "the browser does".

What is owed before this stage can be called done: build `arcium_unittests`
and `arcium_browsertests`, run both, walk the ten hand rows, and measure with
`scripts/perf --label stage5a`.

## What Chromium already had

Split view is graduated upstream and carries no feature flag:
`MultiContentsView` is built in every `BrowserView`, and `TabStripModel` owns
the pair — `AddToNewSplit`, `RemoveSplit`, `GetSplitForTab`, `GetSplitData`,
`GetForegroundTabs`, `ReverseTabsInSplit`, `UpdateSplitRatio`. The divider,
the two panes, the resize, the mini strip and the drop target at the page's
edge are all Chromium's and were not rebuilt.

What Arcium lacked was every way in. Chrome's entry points live in the tab
strip — a tab's context menu, a toolbar button, dragging one tab onto another
— and this browser hides the tab strip, so a graduated feature was reachable
only by dropping a link at the edge of the page. This stage is four ways in,
one rule about who may share a screen, and a record so a split survives a
quit. **No patch was added**; nothing in `patches/` changed.

## What was built

Eight commits, `415a563` to `da16f52`.

**The rule.** `SplitController`, one per window, owned by
`BrowserSidebarController`, answers whether two tabs may share a screen and
refuses five cases: the same tab twice, an index the strip does not have,
either tab already in a split, either one a loose page (a peek, or the small
window an outside link opens in), and two tabs in different spaces. The last
is the one that matters: two spaces can sit on two profiles, which would put
two sets of logins on one screen and hand the partition guard a tab visible
in a space it does not belong to.

**Four ways in.** A sidebar row dragged onto the page, which lights the half
under the pointer and puts the dropped page on that side; the row menu's
"Split with current page"; Cmd+Option+S, which splits with the page you were
on before this one and breaks the split when there is one; and a "Split the
screen" command in the box, which then lists the tabs you could split with and
splits when you take one. The fifth, a link dragged to the page's edge, is
Chromium's and now has a test because nothing else here would notice it
breaking.

**The sidebar.** Both halves read as current, because the model now asks the
strip for its foreground tabs rather than for its one active index. Two rows
in the same split, in the same section and the same folder, and next to each
other, are joined by a bracket the list draws across the gap between them;
a row whose partner is not its neighbour carries a two-pane mark in the slot
the audio indicator uses. Adjacency is computed in the model
(`split_joins_previous` / `split_joins_next`) so no view has to know about any
row but its own.

**Leaving a space.** Moving either half to another space ends the split first,
before the tag changes and before an entry's tab is reopened, so a split whose
halves are in two spaces never exists, not even for one turn of the loop.

**After a quit.** A space records its split as the two tabs' `TabKey`s, the
layout and the ratio. `TabKey` already survives a restart — it is written into
the session file and read back with the tab — so nothing new had to be
invented to name a tab across a launch. Model schema 5 to 6, with the usual
no-op migration step: a file written before this has no split key, and an
absent key is exactly what "nothing was sharing this space's screen" means.

Re-forming is posted rather than done in place, because both things that
trigger it — a tab insertion and a model change — arrive from inside a
notification, and re-forming mutates the strip. It activates one of the two
tabs, because that is what `AddToNewSplit` splits with, and puts activation
back where it found it afterwards; without that, a split recorded in a space
the window is not showing would drag the window into that space at every
launch.

## Five things found while writing, all fixed before they shipped

Each was found by reading the upstream implementation rather than by a test,
which is the method this stage was written under and also its weakest point:
a fifth of the same kind would not have been caught.

1. **`AddToNewSplit` takes one index, not two.** The plan's own code sample
   passed two, and `tab_strip_model.cc` opens with `CHECK_EQ(indices.size(),
   1u)` and `CHECK(active_index() != indices[0])`. It splits what it is given
   with whatever is active. Passing two would have crashed the browser on the
   first split. Corrected before any implementation was written.
2. **`GetSplitForTab` CHECKs its index.** Building a sidebar row from a stale
   index would have crashed rather than drawn nothing, so row builds ask
   `tabs::TabInterface::GetSplit()`, which returns an optional and CHECKs
   nothing.
3. **A returning method cannot be bound to a `WeakPtr`.** The box's "which
   tabs could I split with" callback returns a vector, so
   `base::BindRepeating` with a weak pointer does not compile; it is
   `base::Unretained` with a comment saying the controller owns and outlives
   the box.
4. **The adjacency rule was written twice**, once in the real model and once
   in the playground's fake, which is how the two drift apart. Pulled out into
   `arcium/ui/sidebar/split_rows.{h,cc}`, which both call and which is unit
   tested without a tab strip.
5. **The controller observed nothing.** Adding the model argument in the last
   task changed the declaration and left the definition alone, so the
   constructor took three arguments where its caller passed four — it would
   not have compiled — and it registered with neither the tab strip nor the
   model. That is more than a missing argument: without the strip there is no
   record of which page you were on before this one, so Cmd+Option+S would
   have done nothing, and no split would ever have been written down, so the
   whole of the persistence work above was dead code. Found by reading the
   constructor against its own header while the build was unavailable
   (`dc7849e`). The unit test for the keyboard's previous-page rule already
   exists and would have failed on it, which is the point: nothing here has
   been run.

## Tests written

Counted, not run. 15 in `split_controller_unittest.cc`, 9 in
`split_rows_unittest.cc`, 6 in `split_view_browsertest.cc`, plus additions to
`box_commands_unittest.cc`, `sidebar_views_unittest.cc` (two new, five
existing menu expectations updated because the menu gained a row) and
`command_box_browsertest.cc` (three), and four serialiser tests and one
migration test for the new schema version.

The browser tests cover a split ending when one half leaves its space, a
split surviving a quit, a split coming back in a space the window is not
showing, and a link dropped at the page's edge landing in the space on screen
with that space's storage.

Two of them carry their own control, because what they assert is mostly that
nothing happened. The space-move test asks that the moved tab is in the other
space, which is what the move was for. The relaunch-into-another-space test
asks that the window is still in the space it was quit in — re-forming has to
activate one of the split's two tabs, and every other assertion in it would
read the same on a browser that had simply been dragged into the other space
and left there.

## Deviation

Two panes, not the two to four R5.1 asks for. Recorded as **D5-1** in section
7 of the master spec, with the reason: Chromium's split is exactly two in
three separate places, and three panes means replacing `MultiContentsView`,
which fullscreen, devtools, find-in-page, picture-in-picture, the capture
border and tab dragging all reach. Three and four stay open behind a feature
flag if they are ever built.

## Owed

- Build and run: `arcium_unittests` and `arcium_browsertests`. Nothing in this
  stage has been compiled.
- The ten hand rows in `docs/stage5a-hand-checks.md`.
- `scripts/perf --label stage5a`, recorded in `docs/perf/`.
- One risk with no test: `AddToNewSplit` reorders the strip so the two tabs
  are contiguous, which moves a Today row in the sidebar. The design accepts
  this and no test freezes the resulting order, because that order is
  Chromium's to decide. If the hand pass finds the movement confusing, that is
  a finding rather than a regression.
