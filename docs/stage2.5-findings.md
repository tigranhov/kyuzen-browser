# Stage 2.5 — Entry behaviour: findings

Nested folders (R2.5.1) and a schema migration for the live model file
(R2.5.2). Ten planned tasks, executed subagent-driven with a fresh implementer
per task and a review between. What follows is what the stage cost and what it
taught, not a summary of the diff.

## What shipped

**A folder can hold a folder.** `Folder` gained `std::optional<FolderId>
parent_id`. Depth is capped at `kMaxFolderDepth = 5` — depths 0 through 4.
Moving a folder is refused if the target is the folder itself, a descendant of
it, or would push the moved subtree past the cap.

**Deleting a folder keeps everything in it.** `ArciumModel::RemoveFolder`
reparents entries *and* subfolders to the deleted folder's own parent — one
level up, not to the top level. Arcium has no destructive delete.

**The file is versioned and migrated.** `kModelSchemaVersion` is 2.
`MigrateModelDict` walks a file up one version at a time on the background
sequence that read it, never on the UI thread. A file that cannot be understood
is moved aside rather than overwritten.

**The sidebar draws the tree.** `BuildSidebarFolders` flattens the forest into
pre-order with a depth. Folders drag into and out of one another, and the
context menu offers the same moves.

## The invariant that paid for itself

`folders()` comes back in pre-order with each folder's depth, which makes one
sentence true: **a folder's subtree is exactly the run of folders after it whose
depth is greater than its own.**

Three problems that read as tree traversals became linear scans over a vector:

- indenting a row — read `depth`;
- hiding a collapsed folder's descendants — skip forward while `depth >` mine;
- refusing a drop into your own subtree — is the target inside that run?

The third one matters most, because it runs on the UI thread on every pointer
move during a drag. Had the view needed its own traversal there, the 8 ms frame
budget would have been a live concern instead of a rounding error.

## What the mutation probes actually found

Every rule in this stage was probed by deleting it and confirming a named test
turns red. The rule this stage kept relearning: **a green test proves nothing
until a mutation kills it** — and in every case the bug lived in a
*substitution*, something else quietly doing the job.

- **A stricter rule firing first.** At the original cap of three, the cycle
  check was unreachable: moving a folder into its own descendant always costs at
  least two levels, so the cap refused every cycle before the cycle rule was
  consulted. Its test passed with the rule deleted. The cap moved to five for
  that reason, not for layout — a fact worth keeping straight, because someone
  lowering it on layout grounds would silently make the cycle guard dead code
  again.
- **A map subscript inventing an answer.** The serializer's repair walk read a
  parent with `folder_index[*parent]`. `std::map::operator[]` on a missing key
  *inserts a default-constructed 0*, so deleting the pass that clears dangling
  parents did not fail — the walk read `folders[0]` and produced the right answer
  from the wrong mechanism. `find()` with an explicit miss branch fixed it. The
  fault was in the plan's own code, not the implementer's.
- **A function never called.** A positions test passed with renumbering deleted,
  because `AddFolder` never called it.
- **A guard behind a condition the test never made true.** A drag test passed
  with its guard deleted because `DropRefusedByHeader` is
  `is_entry() && IsOverHeaderAt(y) && !CanFolderAcceptEntry(id)`, and a pinned
  row of the same list is always acceptable — so `IsOverHeaderAt` never decided
  anything. Dragging a *favourite* made the third term false and the guard
  decisive.
- **A mutation that never applied.** One probe "survived" because its pattern
  had eight spaces of indentation where the source has six. It changed nothing,
  built clean, and passed. A probe that does not change the file is not a weak
  experiment; it is no experiment.

## The tooling lied twice, and both lies read as success

**The test launcher's `[N/N]` is not a test count.** It retries tests and
re-runs batches, so identical runs printed 341 and then 336 for the same 332
tests. Every suite size quoted in this stage's early reports came from that
number and overstated. Worse, a test that fails on its first attempt and passes
on a retry disappears into `SUCCESS`. The honest form is
`--gtest_list_tests` minus `DISABLED_`, run with `--test-launcher-retry-limit=0`.

**Probe runs leaked processes that then broke unrelated tests.** A mutation that
removes a loop guard leaves a test that never returns. The launcher times it
out, retries, passes, prints SUCCESS — and the timed-out process keeps spinning
at 100% of a core forever. Two orphans survived over an hour, both running
`ModelSerializerTest.ACycleBetweenFoldersIsBrokenNotFatal`; `sample <pid>` put
them inside `DeserializeModel`'s repair loop. They starved the timing-sensitive
`SidebarViewsTest` and `TabSearchServiceTest` cases into failing, which was
recorded as flakiness and was not. **There is no evidence of inherent flakiness
in this suite.** Both lessons are now rules in the plan's probe-harness section.

## Where Zen settled a question, and where it did not

Partway through, the project owner directed that Zen's implementation is the
source of truth for behaviour questions. Zen is a Firefox fork with the same
sidebar model and it is open source, so three choices that were the plan
author's became checkable. Findings are in `docs/research/zen-folders.md`, read
from Zen's source rather than described.

- **The five-level cap agrees.** `zen.folders.max-subfolders` defaults to 5, and
  Zen's own test asserts the "new subfolder" item is disabled at the fifth level.
- **`RemoveFolder` is Zen's *unpack*, not Zen's *delete*.** Zen has both; its
  delete closes every tab in the subtree. Keeping the contents is right here
  because Arcium's folders hold persisted entries rather than live tabs —
  closing a tab is recoverable, discarding a saved favourite is not, and
  `ModelStore` would write that loss to disk 2.5 seconds later.
- **The subtree count is ours alone.** Zen shows no count on a folder at all, so
  there is no precedent either way.
- **One deliberate divergence.** Zen's drag-time cap checks only
  `target.level + 1` and ignores the height of the dragged subtree, so its cap
  holds when creating a subfolder and leaks under drag. Ours counts the height.
  Recorded with reasoning so nobody later "fixes" ours toward Zen's.

Two Zen behaviours were filed against **Stage 2.6**, whose design is still open:
a tab entering a folder is force-pinned, and folder membership is mutually
exclusive with Essential/Favourite status.

## Defects found by review rather than by tests

- **A real data-loss path.** `MoveUnreadableFileAside` discarded `base::Move`'s
  return value. When the move fails the unusable file is still at the store's
  own path; the model starts empty, and the next mutation's debounced save
  overwrites the user's only copy — the exact outcome moving it aside exists to
  prevent. The store now stops writing for the rest of its life when it can
  neither read nor preserve its file. A session whose changes are not saved is
  recoverable by restarting; an overwritten file is not.
- **Two affordances that lied.** The favourites grid accepted a folder payload,
  opened a gap indicator, tracked the pointer and then no-oped on release. And
  "Move to folder" read as enabled even when every item inside it was greyed
  out, because the submenu carried command id 0 and fell through
  `IsCommandIdEnabled`'s `default: return true`. The second was never only a
  folder problem: two of the three tests it broke were pinned rows.

## Process notes

**Three defects came from the plan's own reference code**, not from the
implementers: the `operator[]` masking, the discarded `base::Move` result, and a
depth-cap test calibrated for a cap of three after the cap moved to five. A plan
detailed enough to paste from is also detailed enough to paste bugs from.

**One implementer stalled and lost its task** by backgrounding a build and then
waiting for a completion notification, which subagents do not receive. Every
later dispatch says so explicitly. `SendMessage` is disabled in this build, so
it could not be resumed.

**Implementers were right to push back.** One found that a brief's own Step 4
code could not pass the brief's own test and changed the design rather than
weaken the test. Another found a probe prediction wrong and reported it instead
of editing code to match. Both were correct.

## What the acceptance pass found

**A2.5.3 passed on a real Stage 2 file.** The live model file happened to still
be at version 1 — one folder, nine entries — so the launch that began the pass
*was* the migration test. Every folder and entry came back, the folder sat at
the top level, no `.unreadable` sidecar was written, and the file was rewritten
at version 2 on the first mutation. That last detail is worth stating: the
migration is a read-path transform, so the version on disk does not change
until something writes. A pass that checks the file immediately after launch
and sees `1` has not found a bug.

**The cycle refusals are correctly invisible.** Dropping a folder onto its own
child, and onto itself, both do nothing — no highlight, no movement. That is
the specified behaviour rather than a missing one: `CanDrop` refuses before any
indicator is drawn, so the drag never offers an affordance it would not honour.
A refusal that animated, flashed or snapped back would be the defect, because
it would tell the user the drop was considered.

**One defect, and it was not a Stage 2.5 defect.** Every cold entry drew a
globe after a relaunch, and kept it until the entry was clicked and a tab
appeared behind it. `ColdFavicon()` returned a placeholder unconditionally, and
nothing anywhere in `arcium/` ever called `FaviconService` — a grep for it
returned exactly one hit, a comment in `archive_list_view.cc` explaining why
that list does not do the lookup. Fixed in `48f19f0`.

**Why no test caught it.** For once, not the substitution shape this document
warns about. Every test that touches a cold row asserts on *rows*, and a row
carries whatever `ColdFavicon()` returned; the assertion was true. Nothing was
wrong with the code under test — the behaviour was simply never specified.
Favicons appear in the spec once, in R1.3, for live tabs. The code comment
claiming cold favicons were "Stage 6 work" was an assertion by the Stage 2
author that the spec does not support, and it read for two stages as though
the question had been settled. **A comment naming a future stage is a claim
about the spec, and is worth checking against it.**

**A note on the machine, not the code.** The suite ran green at 371/371 twice,
then produced two failures and later a timeout that cascaded into eight tests
not run — on code differing only by a comment. Load average was 152 on ten
cores, from a speech-recognition service, Xcode's CoreDevice, WindowServer and
another browser. Every failing test passed in isolation in under 1.5 s. This is
the starvation pattern recorded above, from external load rather than from our
own leaked probes. `--test-launcher-retry-limit=0` is what made it visible:
with retries on, the timed-out test would have been re-run, passed, and printed
SUCCESS, hiding both the timeout and the eight tests that never ran.

## Carried forward

- **Perf was not measured.** `scripts/perf` refuses while an `out/dev` Arcium is
  running, and one belonging to unrelated in-flight work had been up for nearly
  seven hours awaiting a visual check. See `docs/perf/2026-09-09-stage2.5.md`
  for the four questions answered in writing and the command to take the number.
- **The acceptance list is mostly executed.** On 2026-09-09, by hand: A2.5.3
  passed on a real version 1 file; nesting by drag was confirmed; and A2.5.2's
  cycle cases — a folder onto its own child, and a folder onto itself — were
  both confirmed to do nothing at all. **Still unrun:** A2.5.2's depth-cap case
  (a drop that would carry a subtree past five levels) and A2.5.1's
  collapse-the-middle-then-quit-and-relaunch half.
- **Two test files are past the size threshold**: `sidebar_views_unittest.cc` at
  2067 lines and `sidebar_drag_unittest.cc` at 1553. Production files are all
  under 510 — `sidebar_tab_model_entries.cc` is 506 and is the only one over
  500. The natural seam in the drag file is the ~250-line folder-drag block.
- **Stage 2.6 inherits two Zen findings** recorded in the spec, above.

## The acceptance pass, what is still to run by hand

- **A2.5.1** Build a folder three deep by dragging. Collapse the middle one and
  confirm the deepest disappears with it while the outer stays. Quit with Cmd+Q,
  relaunch, confirm the tree and the collapsed state survived.
- **A2.5.2** — *cycle cases done 2026-09-09, passed.* Still to run: the
  depth-cap case. Drag a folder that is itself two deep onto a folder at the
  fourth level; the drop is refused on the height of the moved subtree, not on
  the target's depth alone, which is the one place this deliberately diverges
  from Zen.
- **A2.5.3** — *done 2026-09-09, passed.* Kept here for the next rebase; a
  copy of the version 1 file used is worth keeping, because a profile only
  offers one.

Also by hand, because no test here can: the drag image carries the folder's
name; dropping a folder on empty space below the Pinned list returns it to the
top level; "Move to folder ▸" never offers the folder itself or anything inside
it; deleting a folder with a subfolder brings the subfolder up one level rather
than to the top.
