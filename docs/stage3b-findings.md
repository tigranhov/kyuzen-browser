# Stage 3b — Profiles as storage partitions: findings

A profile is a storage area inside the one Chromium profile, and a space
points at one, so the same site can be signed in as two accounts in two
spaces, surviving relaunch. Thirteen planned tasks, executed subagent-driven
with a fresh implementer per task and a review between; this document is
task 13, the automated and documentation half of the close-out. What follows
is what the stage cost and what it taught, not a summary of the diff. The
full task-by-task record, every ruling and every defect's trace, lives in
`.superpowers/sdd/2026-09-11-stage-3b-profiles/progress.md`; this document
distills it for a reader who was not there.

## What shipped

**A profile is an id and a colour; its storage is a partition domain derived
from that id.** `ArciumProfile` lives beside `Space` in the model
(`arcium/browser/model/arcium_profile.h`), one JSON file as before, migrated
on read. The default profile is Chromium's own default storage partition and
has no directory of its own; every other profile is
`StoragePartitionConfig::Create(context, "arcium-<id>", "", /*in_memory=*/false)`
(`arcium/browser/profile_partition.cc`), so erasing a profile erases exactly
one domain and a profile id can never collide with an extension's (32-letter)
or an isolated web app's (`i`-prefixed) partition domain.

**Every tab-creation path is covered, by a hook per path plus a guard that
catches what the hooks cannot see coming.** A new tab, a popup with an
opener, a restored tab, a reopened closed tab and a discarded tab's
replacement each get a `SiteInstance` fixed to their space's profile through
one of five upstream hooks (patches 0181, 0182, 0184, 0185, 0190) delegating
to `arcium/browser/profile_partition.cc` and `arcium/browser/tab_space.cc`.
A `target=_blank` popup with **no** opener, a browser or extension page typed
into a profile's tab, and anything the hooks cannot reach are caught instead
by `PartitionGuardThrottle` (patch 0186), a `NavigationThrottle` that compares
the committing page's required storage against the tab's actual partition and
posts a reopen in the right one when they disagree — posted, not synchronous,
because the throttle can run from inside a tab-strip operation that is not
reentrant (the crash `Check failed: !*guard_flag` at
`tab_strip_model.cc:141`, found and fixed in task 6).

**Managing profiles is model operations plus a menu.** Create, rename,
recolour and delete a profile; move a space to one; clear one profile's data
or every non-default profile's — all in `arcium/browser/profile_data.cc` and
`arcium/ui/browser/profile_actions.cc`, reached from a Profile submenu on
every space's row (`arcium/ui/sidebar/profile_menu.cc`). Deleting a profile
moves its spaces to Default and logs them out; it never deletes Default
itself, guarded twice — once because `ArciumModel::RemoveProfile` refuses the
id, and independently because `DeleteArciumProfile` refuses to obliterate any
partition domain that is not an Arcium profile's, so even if the first guard
were ever removed the browser's own shared cookies could not be reached.

**Two upstream risks resolved, both load-bearing.** Chromium's storage-
partition garbage collection deletes any partition directory it does not
recognise and is not holding open after an extension is removed — patch 0188
hands it the keep list built from the model's own profiles, computed from ids
with no disk listing, and skips the sweep for one launch entirely rather than
run it against an incomplete list (a malformed profile row now fails the
whole model read, precisely so this list can never be silently short).
Chromium never restores session cookies outside the default partition; patch
0190 lets an Arcium partition follow the profile's own "continue where you
left off" setting, which A3.1 requires.

**Chrome's own Clear browsing data gets a warning it did not have.** Patch
0192 hooks the settings page's cookie-clearing confirmation so that clearing
cookies offers two answers — "shared logins only" or "every profile too" —
with no cancel, because the settings page's own script reports success the
moment its promise resolves and has no way to represent a decline.

## What the acceptance pass found

The eleven-line list is the owner's to run by hand — signing into two real
accounts, dragging tabs between profiles, installing and uninstalling a real
extension, watching Activity Monitor — and this task does not pretend to
have done it. Renumbered to the design's own A3b.1-A3b.7 per the pre-flight
ruling, plus the master spec's A3.1 and A3.3 and the two informal checks the
brief named "Guard" and "Browser pages".

| Check | What must happen | Result |
|---|---|---|
| A3b.1 (automated) | Every way a tab can be made lands in its space's own logins | **PASSED** — `arcium_browsertests`, `ProfileIsolationTest`, `ProfileRestoreTest`, `ProfileLifecycleTest` and `SidebarTabModelProfilesTest`, 31 unique tests, run twice, all green both times |
| A3b.2 | Drag a signed-in tab to a space on another profile: it stays where dropped, keeps its page and history, and is signed in as the space it landed in | NOT RUN — owner |
| A3b.3 | Clear one profile from the space menu: that space is signed out, every other space untouched, no page reloads | NOT RUN — owner |
| A3b.4 | Delete a profile, quit, relaunch: its spaces stay, moved to shared logins and signed out; gone from the menu and still gone after relaunch | NOT RUN — owner |
| A3b.5 | Chrome's Clear browsing data with cookies selected and a second profile in use: warning appears and says it cannot be undone; "shared logins only" leaves the second profile signed in; "every space too" logs both out | NOT RUN — owner |
| A3b.6 | Install an extension, uninstall it, launch twice: every profile still signed in, both times | NOT RUN — owner |
| A3b.7 | An extension's options page opened from a profile space and from a Default space show the same saved settings | NOT RUN — owner |
| A3.1 (owner) | Sign in to the same site as two different accounts in two spaces, quit, launch again: both accounts still signed in, each in its own space, nothing loads until a tab is clicked | NOT RUN — owner |
| A3.3 | Watch memory and process count with three spaces and two profiles open: no process per profile while nothing is loaded, no growth per background space | NOT RUN — owner |
| Guard | Open a link with no opener in a profile space: it opens in that space's profile and leaves no stray tab | NOT RUN — owner |
| Browser pages | Type a settings address into a tab in a profile space: it opens normally, in shared storage, no empty tab left behind | NOT RUN — owner |

A3b.1's automated half is the one line this task can and did answer: both
suites green twice, with the previously-flaky
`ProfileLifecycleTest.DeletingAProfileMovesItsSpacesAndLogsThemOut` clean on
both runs of this pass (no recurrence of the parallel-load crash task 9's
review recorded).

## Two things settled this task, neither by reading alone

**The deliberately-failing test asserted the wrong thing, and now asserts
the true one.** `ARestoredTabBuildsNoStorageUntilItLoads` (task 4) checked
that a profile with a restored tab builds no storage until that tab loads.
Traced in the task 4 review with citations into Content: a fixed-partition
`SiteInstance` cannot join its `BrowsingInstance`'s default site instance
group, so `WebContentsImpl`'s constructor forces a process lookup that
reaches `GetStoragePartition` with creation permitted, regardless of
`kNoRendererProcess`. The storage is built the moment the tab is created, not
when it is clicked. Renamed to
`ARestoredTabBuildsStorageAtCreationNotAtLoad` and rewritten to assert that,
so it still fails if this ever changes back; its first check stayed
`EXPECT_TRUE` rather than `ASSERT_TRUE`, with a comment saying why — the
click that follows is worth checking even if that first line regresses.

**Whether a real renderer process is spawned per restored tab was answered
by counting, not by reading.** The open question the ledger carried into
this task: `GetOrCreateProcess` and `CreateRenderProcessHost` run in that same
path, and a process *host* is an object, while an OS process is only spawned
when a renderer is actually needed. If one were spawned per restored tab in
its own profile, R3.9's no-extra-processes promise would be broken, which
would be a far bigger finding than the storage one. Settled by a new
browser test, `ARestoredTabsProcessIsNotSpawnedUntilClicked`: before the tab
is clicked its `RenderProcessHost::IsInitializedAndNotDead()` is false and
`RenderProcessHost::GetCurrentRenderProcessCountForTesting()` — Content's
own count of renderer processes that actually exist, spare renderer
excluded — does not include it; after activation both flip. **No process is
spawned until the tab is clicked. R3.9's process promise holds; its storage
promise does not.**

Both tests are in `arcium/test/browser/profile_restore_browsertest.cc`,
green in both browser-test runs of this pass.

## Corrections made to the project's own documents

- The master design's §4.4 and its risk table described profiles as they
  were imagined — a fixed partition with popups and child frames inheriting
  it, `SiteInstance` isolation as an open risk. Corrected to what was built:
  a profile is a storage area named by an id; only cookies, site storage and
  cache are separate, everything else shared; moving a tab between profiles
  reopens it fresh, losing anything unsaved; a session cookie survives a
  quit in every profile, which needed an upstream change; Chrome's own Clear
  browsing data reaches shared logins only unless the wider answer is
  chosen. Both risk-table rows for Stage 3(b) are marked resolved, with how.
- The stage 3b design's own §12 made the same mistake more specifically: it
  claimed nothing is built for a profile none of whose tabs has loaded. That
  is false for a profile with a restored tab, corrected above. Only a
  profile with **no restored tab at all** costs nothing until it is opened.
- `CLAUDE.md`'s Stage 3 row is updated for the 3b sub-line: what has been
  proven so far (the automated pass, twice green; the two settled
  questions), and what has not (the eleven-line hand pass and A3.3's
  measured per-partition cost, both the owner's to run).
- **`README.md` does not exist in this repository** — there is no top-level
  README, tracked or untracked, and none appears anywhere in this repo's git
  history. The brief's instruction to update "the Stage 3b block written in
  Task 0" does not apply here; nothing was created or changed for it, and
  this is flagged rather than silently skipped.

## What is known to be imperfect, and was left alone

- **A tab that changes profile loses anything unsaved on the page, and its
  per-tab storage, because it is genuinely a new page.** Moving a space to a
  new profile, or moving a tab between profiles, reopens every affected tab
  at its last address with its back history; nothing asks the page whether
  it may close, so a draft in a text field or an in-memory upload is gone.
  This is the honest cost of a fixed-partition `SiteInstance` requiring a new
  `WebContents`, not an oversight.
- **Preloading is off inside a profile's storage**, so a page Chrome would
  otherwise have started loading ahead of a click is loaded when it is
  clicked instead. `PrerenderEligibilityForTab` refuses prerender outright
  for any tab on an Arcium partition domain (§5.4 of the design), because a
  prerendered frame tree is always built in the default partition and
  activation would swap the tab into the wrong storage.
- **Deleting a profile something opened this session leaves an empty folder
  behind until a later launch removes it.** `DeleteArciumProfile` obliterates
  the partition's storage asynchronously and returns; the directory itself
  is only reaped by Chromium's own storage-partition cleanup at a later
  startup, once nothing holds it open. The spaces move to Default and log
  out immediately; the disk cleanup is deferred, by design (§7.1's keep-list
  hook is the same mechanism that would otherwise have erased it early).
- **Chrome's own Clear browsing data cannot be stopped once its button is
  pressed, and this is forced, not a design shortcut.** The settings page's
  own script reports success to Arcium's hook the instant its JavaScript
  promise resolves; it has no channel back to Arcium for "the user declined"
  because Chromium's UI never asked Arcium's hook in the first place — the
  hook only observes the outcome. So the choice has to be made before the
  irreversible action starts, which is why the warning offers exactly two
  answers ("shared logins only" or "every profile too") and no cancel: a
  cancel would have nothing to cancel by the time it could be shown.
- **`DeleteProfileCache` is deliberately unverified by any test.** A test
  was written for it in task 8's review round — create the cache directory a
  deletion should remove, delete an unopened profile, wait for the
  directory to go — and then deleted after being run against a mutated build
  with the function's entire body removed: **the test still passed.** The
  reason is in the function's own comment: under a browser test's temporary
  user-data directory, `chrome::GetUserCacheDirectory` cannot map the
  profile path to a separate cache location and hands back the path it was
  given, so the computed cache path collapses onto the profile's own
  partition directory — the one `AsyncObliterateStoragePartition` already
  deletes. The test was watching the obliterate, not the cache removal. The
  separate cache tree this function exists for only appears in a real
  profile on macOS, which no browser test builds. Correct by inspection
  against Chromium's own cache-path derivation (confirmed independently in
  the task 8 review); simply not provable at this seam.
- **A test that waits for a navigation nobody starts does not fail — it
  hangs for thirty seconds and reads as a timeout.** This branch lost time to
  this shape four times: an already-active tab whose reactivation is a
  no-op, a discarded tab Chromium declines to reload, a restored session
  tab already on the space under test, and a `PRE_` test that leaves the
  window already on the tab its body expects to watch load. Every case was
  fixed by finding out whether a navigation actually starts (and waiting on
  the load already in flight with `content::WaitForLoadStop` when it does
  not) rather than adding more waiting.
- **Adding a decorative view to the sidebar needs an explicit accessibility
  role before it can be given a name.** The profile badge DCHECK-crashed
  every space-bar test at construction until it was given one; a bare view
  in this codebase cannot carry a name without it.
- **An incognito window gets a full sidebar with its own unpersisted model,
  because sidebar creation checks only for a normal-type window
  (`BrowserSidebarController::MaybeCreate` gates on `is_type_normal()`, never
  on `IsOffTheRecord()`).** Making a profile there adds it to that transient
  model, but `MoveSpaceToProfile` refuses off-the-record outright, so
  attaching it always fails. `SidebarTabModel::CreateProfileForSpace` now
  undoes the add when the attach refuses, so the attempt leaves the model
  exactly as it found it rather than lingering as an orphan profile that can
  never be reached. Fixed in the sidebar layer, not the menu, because the
  menu is only the caller that makes the half-finished creation reachable.

## Defects found and fixed during implementation (from the ledger, for a reader who was not there)

Everything below was already fixed before this task began; recorded here
because a later reader of the diff would otherwise have to reconstruct why
each fix exists from the commits alone.

- A discarded tab lost the space it belonged to, because the discard
  replaces the `WebContents` and nothing copied the space tag or tab key to
  the replacement. Fixed with `CarryTabIdentityTo`, called from a new
  `kReplaced` branch in `SpaceSwitcher::OnTabStripModelChanged`.
- The partition guard could crash the browser by mutating the tab strip
  from inside a navigation throttle running inside a tab-strip operation
  (`Check failed: !*guard_flag`, `tab_strip_model.cc:141`). Fixed by posting
  the relocation instead of performing it synchronously.
- `DefaultCannotBeDeleted`'s own mutation check does not fail, because
  `ArciumModel::RemoveProfile` already refuses Default one layer below
  `DeleteArciumProfile`'s own check — accepted, but the unguarded path was
  additionally hardened so it can never reach
  `AsyncObliterateStoragePartition("")` against the browser's real default
  storage, whichever guard is ever weakened.
- A malformed profile row in the model file used to be silently dropped
  while the read still reported success, which would have handed
  Chromium's storage cleanup a keep list missing a real profile and erased
  its logins for good. Fixed so a malformed profile row fails the whole
  read; space, folder and entry rows stay lenient, unrelated to this stage.

## Performance

Measured `2026-09-12`, machine load falling from ~47 to ~10-11 across the
run (`docs/perf/2026-09-12-stage3b.md`). Full analysis is in that file; in
short, two of the four claims the brief carried forward from the design
turned out to be true only for a profile with **no** restored tab, and the
note says so rather than adjusting the wording:

1. **Process per background profile?** No — proven by count, not assumed.
2. **Idle memory unchanged, cost equal to a profile's open pages?** True only
   for a profile nothing has restored; a restored tab's profile pays for its
   storage and network context before any page in it has "loaded" in the
   ordinary sense.
3. **Startup does no more than before, nothing asks for a profile's storage
   until a tab loads?** False: a restored tab's profile is asked for its
   storage at creation, during session restore — at startup, not deferred to
   a click.
4. **Nothing new on the UI thread per frame beyond a string comparison in
   the guard?** Holds, by inspection; not independently re-traced this
   session.

`scripts/perf`'s method (a fresh profile, `about:blank`) cannot exercise the
restored-tab case at all — there is no model file yet — so it answers only
the "profiles never used" baseline (unchanged from Stage 2, within noise)
and the browser test is what proves the restored-tab and process-count
claims. A3.3's own measured per-partition cost with three spaces and two
profiles open is the hand pass's to take.

## Out of scope, unchanged from the design

Per-profile history, passwords or extensions; Air Traffic Control's routing
rules; a site-data view per profile; syncing profiles. All named already in
§13 of the stage 3b design and untouched by this task.
