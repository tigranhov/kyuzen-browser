# Stage 5a: two pages on one screen

R5.1. Design: `docs/superpowers/specs/2026-09-22-stage-5a-split-view-design.md`.
Plan: `docs/superpowers/plans/2026-09-22-stage-5a-split-view.md`. Hand rows:
`docs/stage5a-hand-checks.md`.

## Where this stage stands

**Built and green, hand rows not walked.** The stage was written in one pass
against headers read in the Chromium checkout, with no compile and no test run,
because the machine was in use. It was built and run for the first time on
2026-09-22, once the owner said the machine was free. The first build and run
found seven more things, listed below, and all seven are fixed. 741 unit tests
pass, and all 87 browser tests passed in one sitting three times running, two
of those runs needing a retry each (see the section on flakes).

What is still owed: the ten hand rows in `docs/stage5a-hand-checks.md`.

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
   (`b949f8c`). The unit test for the keyboard's previous-page rule already
   exists and would have failed on it, which is the point: nothing here has
   been run.

## Seven things found by the first build and run

Two stopped the build, one was a product defect a hand pass would have hit,
and four were defects in the tests.

1. **The page drop target did not compile.** It used four types it never
   included: the compositor layer, the layer tree owner, the drag operation
   and `base::NullCallback`. Reading headers checks that a function exists,
   not that the file including it can see it (`9079948`).
2. **The controller's weak pointer factory was not its last member**, which
   Chromium's style check flags because a factory destroyed after the members
   it guards can hand out a pointer into a half-destroyed object (`9079948`).
3. **The box's split question was overwritten by late answers.** Taking
   "Split the screen" lists the tabs you could split with, but the ordinary
   suggestions for "split" were still arriving -- Chromium's slower providers
   answer last -- and each late batch replaced the tabs on offer, so Enter took
   a history row instead of a tab. The box now ignores its source while it
   asks, and stops the source when the question opens. Both box split tests
   failed on this in the first run (`798266f`).
4. **The version 4 migration test pinned the result to version 5**, and this
   stage made the current version 6. It now compares against the current
   version, as the other migration tests do (`9079948`).
5. **Six folder tests counted the split bracket as a row.** The bracket is a
   hidden child of the tab list, kept last and outside layout. Nothing in the
   product walks those children, so the test helper skips the bracket rather
   than the list dropping it (`9079948`).
6. **Both relaunch tests compared whole addresses**, and the test server takes
   a new port on every launch. The split had come back correctly in both --
   including in the space the window was not showing, with the window left
   where it was -- so they now compare host and query (`798266f`).
7. **The window drag test's full-screen case never went full screen.** It was
   written earlier the same day, outside this stage, and waited for a real
   macOS full-screen animation, which the system finishes only for a window
   it has in front; the wait timed out whether the test ran alone or in the
   suite. It now fakes the transition with `ScopedFakeNSWindowFullscreen`, as
   Chromium's own full-screen tests do (`f3170c8`).

## Tests

15 in `split_controller_unittest.cc`, 9 in `split_rows_unittest.cc`, 6 in
`split_view_browsertest.cc`, plus additions to `box_commands_unittest.cc`,
`sidebar_views_unittest.cc` (two new, five existing menu expectations updated
because the menu gained a row) and `command_box_browsertest.cc` (three), and
four serialiser tests and one migration test for the new schema version. All
pass.

The browser tests cover a split ending when one half leaves its space, a
split surviving a quit, a split coming back in a space the window is not
showing, and a link dropped at the page's edge landing in the space on screen
with that space's storage.

Two of them carry their own control, because what they assert is mostly that
nothing happened. The space-move test asks that the moved tab is in the other
space, which is what the move was for. The relaunch-into-another-space test
asks that the window is still in the space it was quit in -- re-forming has to
activate one of the split's two tabs, and every other assertion in it would
read the same on a browser that had simply been dragged into the other space
and left there. That control passed in the first run, before the port fix,
which is how the relaunch half is known to have worked from the start.

Watched to fail: the two box split tests (before the late-answers fix), the
two relaunch tests (on the port, with everything else in them passing), and
the full-screen test. The controller's constructor defect from the writing
pass was fixed before anything was built, so the keyboard test was never seen
to fail on it.

## Flakes

Two runs out of three needed one retry each, and the tests involved differ.

- **Two of the box's split tests** found the box closed straight after Enter
  in the first run of the suite, in its first batch, when ten browsers start at
  once and every test took 14 s instead of 5. Taking "split" closes the box
  for three reasons only: the top row was not the command, no tab could share
  the screen, or the box lost focus, which closes it by design. The tests now
  print all three if it happens again (`be48a98`). Twenty-four parallel runs of
  the three tests and two more full runs did not bring it back, so the cause
  is not known.
- **`PillTest.APermissionRequestStillHasSomewhereToAppear`** timed out once
  and passed on retry. It is Stage 4a's and nothing here touches it.

## Deviation

Two panes, not the two to four R5.1 asks for. Recorded as **D5-1** in section
7 of the master spec, with the reason: Chromium's split is exactly two in
three separate places, and three panes means replacing `MultiContentsView`,
which fullscreen, devtools, find-in-page, picture-in-picture, the capture
border and tab dragging all reach. Three and four stay open behind a feature
flag if they are ever built.

## Owed

- The ten hand rows in `docs/stage5a-hand-checks.md`.
- One risk with no test: `AddToNewSplit` reorders the strip so the two tabs
  are contiguous, which moves a Today row in the sidebar. The design accepts
  this and no test freezes the resulting order, because that order is
  Chromium's to decide. If the hand pass finds the movement confusing, that is
  a finding rather than a regression.
