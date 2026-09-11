# Stage 3a — Spaces: findings

Spaces are a side table over one window's tab strip, not extra windows and not
Chromium tab groups. Thirteen planned tasks, executed subagent-driven with a
fresh implementer per task and a review between, then one whole-branch review,
one fix wave and a hand pass. What follows is what the stage cost and what it
taught, not a summary of the diff.

## What shipped

**A space is a row in the model and a tag on a tab.** `Space` lives in
`arcium/browser/model/`, beside `TabEntry` and `Folder`, in the same JSON file,
migrated from version 2 to version 3 on read. A tab's space is user data on the
`WebContents`; an entry's space is a field. `SpaceOfTab` resolves the entry's
space first, then the tab's tag, then the first space, so a tab that arrives
with no opinion still has one.

**One switcher per window holds the active space.** `SpaceSwitcher` answers
"is this tab in the active space", performs switches, and remembers which tab
each space was last on. The sidebar model, the archive service and tab search
read the active space from it. Background spaces have no timer, no observer and
no thumbnail of their own: the one observer is per window.

**Two upstream hooks, both a few lines.** `0155` scopes eleven strip-wide tab
commands to the active space at the seam `0090` already uses. `0160` asks a new
defaulted `TabStripModelDelegate` method which tab to activate when the active
one closes, because `tab_strip_model.cc` builds below the only target that
reaches `arcium/`. Every other delegate behaves exactly as before.

**Deleting a space closes its tabs the normal way.** Its tabs close through
Chromium's own close, so a page with a `beforeunload` handler still gets to
ask; survivors are re-tagged by handle identity; its archived rows are deleted
on the store sequence, and its parked rows are dropped first.

**The bar is Views, like everything always visible.** A chip per space with
rename, icon, colours, reorder and a confirmed delete; per-space gradients;
Ctrl+1..9; a two-finger swipe; a 200 ms slide; and "Move to space" on every
row.

## What the acceptance pass found

Executed by hand by the owner on 2026-09-11, against the build at `90729c8`.
A3a.1 through A3a.8 and A3.2's shape all passed. Three of them raised a
question about behaviour, and all three answers were the specified behaviour
rather than a defect:

**Cycling skips cold rows, by design.** Ctrl+Tab and Cmd+1..8 walk the active
space's *open* tabs in sidebar order. A cold pinned or favourite row is not a
tab, and §4.2 says so explicitly: a cold row's place depends on which folders
are collapsed, which only the view knows, so a count made in the browser layer
would disagree with the screen. Cmd+W on a pinned or favourite tab closing it
to a cold row is Stage 2's rule, unchanged: the entry outlives its tab.

**Cmd+click opens a background tab in the current space.** A3a.6 asks only
that the new tab belong to the space you are in. Not jumping to it is
Chromium's own behaviour for a background tab, which Arcium does not override.

**Cmd+Shift+T takes you to the tab's space.** §4.4 is the rule: activating a
tab of another space switches to it, wherever the activation came from. A
restored tab is an activation like any other, so the window follows it. This is
also how §4.4's rule was checked, since nothing in the shipped UI reaches
`TabSearchService` yet.

**One defect, in a sentence rather than in code.** After deleting a space,
Cmd+Shift+T brings its Today tabs back — they closed through Chromium's normal
close, which creates historical tabs — while its pins and favourites are gone
for good, because their entries were deleted from the model. The behaviour is
right; the confirmation's "This action cannot be undone" was not. Reworded so
it promises only what is true, in `0eeee06`.

## What the whole-branch review found, and why the tests did not

Four findings, all fixed before the hand pass. The first is the one worth
remembering.

**A tag written only on a rebuild is a tag the file may never see.** The space
tag and the tab key reach the session file through one writer,
`AppendTabSpaceCommands`, which runs inside `SessionService::BuildCommandsForTab`
— that is, only on a rebuild. Stage 2 knew this and installed a nudge that asks
for a rebuild when a binding changes; nothing in this stage asked for one when
a tab was tagged, moved, re-tagged or given a landing key. So a space made and
used for a minute before quitting came back with its tabs in the first space.
Fixed in `993762c` by requesting the same coalesced rebuild whenever a live
tab's tag changes or a key is minted.

**Why no test caught it.** `ARebuildWritesTheSpaceTagAndTheTabKey` calls the
writer directly. It exercises the real writer and not the real trigger, which
is the test-helper shape this project has now been bitten by twice: a helper
that stands in for the production path hides a difference in that path.

**A rule enforced in one place is enforced nowhere.** "A space never runs out
of tabs" was implemented in the Cmd+W path only, so closing a space's last tab
from a row, a row menu or the Clear button moved the window to another space.
The test now lives in one switcher method that all four callers ask first
(`b0a33c5`). The plan assigned only the Cmd+W path, so this is a plan gap
rather than an implementer slip.

**A window that starts on a placeholder must not undo session restore.** The
switcher is built before the model file has loaded, so every launch ran a
fallback switch after restore and could replace the tab restore had selected —
on a Stage 2 profile, with the first favourite. The fallback now adopts the
restored selection when it already belongs to the space the window should show
(`cb6644c`).

**Teardown rules that only a comment enforced.** The archive service and the
switcher pointed at each other, and the wiring task was to break the link by
hand in the right order. The service now registers and clears itself
(`e94fd20`), the observer list checks it is empty, and the per-strip registry
checks its own bookkeeping, so the window wiring cannot get the order wrong
silently.

## Performance

The four questions, answered against what the build shows rather than against
intent. `scripts/perf` was not run: the machine's load average sat above 700
for the whole stage, and a measurement taken there says nothing. Run it when
the machine is quiet, before Stage 3b.

1. **A process, or one kept alive longer?** No. No WebUI surface, no new
   service, nothing per space.
2. **Idle memory?** Per window, one `SpaceSwitcher`: the active space id, a
   small map of last-active tabs, and an observer registration. Per tab, two
   short strings of user data, the space tag and the key. Per space, a row in
   the model that was already loaded.
3. **Work before first paint?** The switcher's construction is a map insert and
   two observer registrations. The model file is read on a background sequence,
   as before, and the fallback switch runs on its own turn after restore, not
   during it.
4. **UI-thread work not needed for the frame?** Three known costs, all of them
   small and none of them measured yet. A tag change or a key mint asks for a
   session rebuild, coalesced and posted, which walks every tab once per burst —
   the same trade Stage 2 made for pins. `spaces()` counts a space's open tabs
   by walking the strip once per space on every coalesced notification. Every
   tab activation records the space's last-active tab, which marks the model
   dirty and schedules a save.

## Carried forward

- **Deleting a space reaches only its own window.** With several windows open,
  the deleted space's tabs in another window survive and resolve to the first
  space, and the confirmation's count understates. The spec is silent on
  several windows; decide the rule before Stage 3b.
- **Nothing in the shipped UI reaches tab search.** `TabSearchService` is space
  aware and tested, but only tests construct it. The command bar in Stage 4 is
  where it becomes reachable.
- **Ctrl+1..9 collides on Windows and Linux**, where those keys select tabs.
  macOS ships first and the spec chose the keys; revisit with the first
  non-macOS build.
- **A background tab tagged with the first space, never activated, followed by
  a space reorder before any rebuild**, restores into whatever is first at
  relaunch. A consequence of not asking for a rebuild when a fresh first-space
  tab is tagged.
- **Launching with "open the New Tab page"** leaves that page hidden behind the
  blank tab of the space the window opens on, when the last active space is not
  the first one.
- **Four files sit over the 500-line guideline** (`arcium_model.cc`,
  `archive_service.cc`, `sidebar_tab_model_entries.cc`, `space_bar_view.cc`)
  and two test files joined them. Each split needs a `BUILD.gn` edit, so they
  are batched into the next change that touches one.
- **The offscreen snapshot cannot paint the sidebar's scrolling list**, which
  has been true since 2026-09-08. It verifies the view tree, not the pixels.

## Process notes

**Three implementers stalled on the same thing.** A build past 600 seconds is
moved to the background by the harness, and each of those three read that as
permission to end its turn, leaving finished work uncommitted. The fix that
held was a first line in every later dispatch: never end a turn while a build
runs, wait it out in the foreground, re-run the build to confirm. Nothing about
the build was wrong; the contract with the worker was.

**The gate held.** Task 12 was gated on three files holding the owner's
uncommitted work. Every other task ran to completion, the gate was reported
rather than worked around, and the wiring took one dispatch once the files were
released.
