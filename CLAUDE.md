# Arcium

A Chromium-based browser that reproduces the Arc / Zen browsing model (vertical sidebar with
Favorites, Pinned and Today tabs, Spaces bound to isolated Profiles, command bar, split view)
while being lighter and faster than Chrome.

Master design and stage requirements: `docs/superpowers/specs/2026-09-05-arcium-browser-design.md`.
Per-stage implementation plans: `docs/superpowers/plans/`. Read the spec before any work.

## Modus operandi

### Performance is the product

Before building any feature, answer these four questions in the plan or PR description.
If any answer is "yes" without a written justification, redesign the feature.

1. Does it add a process, or keep one alive longer? (Budget: zero extra processes while no WebUI surface is open.)
2. Does it add idle memory, per window or per tab? (Budget: +20 MB per window over vanilla Chromium, 0 per tab.)
3. Does it add work before first paint at startup? (Budget: +50 ms over vanilla.)
4. Does it do work on the UI thread that is not needed for the frame being drawn? (Budget: 8 ms per frame; no sync I/O ever.)

Run `scripts/perf` at the end of a stage and record the result in `docs/perf/`. The budgets in the
spec (section 3) are provisional until Stage 0 produces a baseline; until they are calibrated a perf
result is discussed, not a gate. Never let perf tooling block feature progress: the machine is shared
and often loaded, so measure when it is quiet and keep the script quick to run and quick to skip.
The four questions above always apply; they cost nothing to answer.

### Architecture rules

- All Arcium code lives in `arcium/`. Upstream Chromium files are changed only through `patches/`,
  and a patch is a hook that calls into `arcium/`. Logic never lives in a patch.
- Always-visible UI (sidebar, command bar, split view, URL pill) is Chromium Views in C++. Never WebUI.
- WebUI is allowed only for on-demand, document-like surfaces (settings, library, new tab page).
  Its process must exit when the surface closes.
- No Swift, AppKit, Cocoa or platform-specific UI code. If a platform needs something, it goes behind
  Chromium's existing platform abstractions.
- One window, one `Browser`, one `TabStripModel`. Spaces are a side table over tabs, not extra browsers.
- Profiles are storage partitions inside the single Chromium profile. Never spawn Chromium profiles for spaces.
- Persistence: JSON via `ImportantFileWriter` for the live model, SQLite via `sql::Database` for the archive.
  Never write on the UI thread.
- Background spaces do nothing: no timers, no polling, no thumbnails.

Hooks. Every upstream change is a numbered patch in `patches/`, applied by `scripts/sync` in
lexical order. A patch starts with a plain-text header naming the seam, the reason, and the
`arcium/` function it delegates to, before the first `diff --git` line. A hook is a few lines that
call into `arcium/`; if a patch starts to carry logic, move the logic into `arcium/`. Two kinds of
patch carry no call at all and say so in their header: GN wiring and registration tables that
Chromium keys by file name. No two patches may add lines to the same part of a file:
`scripts/sync` decides a patch is already applied by reversing it, and a patch whose context
holds another's additions can no longer be told apart from a conflict, so every run reports one.
Put such changes in one patch, or far enough apart to clear the three lines of context.

### Development workflow

1. Brainstorm and get approval before implementing (superpowers:brainstorming). No code before a yes.
2. Each stage gets a plan in `docs/superpowers/plans/` (superpowers:writing-plans) derived from the
   stage's requirements in the spec. Execute plans task by task.
3. Test-driven: unit tests in `arcium/test/` for models and services, browser tests for tab-to-space
   mapping, partition isolation and persistence. Write the failing test first.
   The whole browser suite takes over the screen and the pointer for several minutes, so it is
   skipped by default: run the few tests a change touches, and before running the full suite ask
   whether the machine is free. Say so in the report when it was skipped, and which tests did run.
4. Iterate on Views UI in the standalone Views playground first, then wire into the browser.
5. Verify before claiming done: build passes, tests pass, the stage's acceptance list executed by
   hand, perf result recorded. Report failures with output, not summaries.
6. Commit small. Commit messages say why. Do not commit Chromium sources or build output.
7. Releases are made only when the owner asks for one. `scripts/release` publishes nothing
   without `--publish`, and nothing is ever published on an agent's own judgement, however ready
   the work looks. See docs/releasing.md.

### Upstream cadence

- `CHROMIUM_VERSION` pins the upstream stable tag. Rebase on every stable release (4 weeks) with
  `scripts/rebase`. Never skip a release; skipping two makes the third painful.
- After a rebase: build, run tests, run perf, execute the last completed stage's acceptance list.
- If a patch conflicts on rebase, first ask whether the hook can move to a more stable seam.

### Coding conventions

- Chromium C++ style, enforced with `scripts/format`. `git cl format` does NOT work in this
  setup: it formats files tracked by the Chromium repo, while Arcium's sources live in this repo
  and are reached through the `chromium/src/arcium` symlink, and it wants a merge base against
  `origin/main` that a shallow checkout pinned to a tag does not have. `scripts/format` calls the
  same two formatters directly.
- Files small and single-purpose. A file over ~500 lines is a smell; split it.
- Names are user-facing concepts: `Space`, `ArciumProfile`, `Favorite`, `PinnedTab`, `TodayTab`, `Folder`.
- Feature flags for anything user-visible and unfinished: `arcium/common/features.h`.

## Environment

- Chromium checkout: `/Volumes/Texternal/chromium/src` (external USB SSD, APFS). The internal disk
  is too small; never put the checkout or build output there.
- `chromium/src/arcium` is a symlink to this repo's `arcium/`.
- **One checkout, one owner.** That symlink points at whichever working tree ran `scripts/sync`
  last, and `out/` is shared too, so two sessions working in two worktrees cannot build at the
  same time: the second one's sync silently redirects the first one's next build at the wrong
  sources, and it fails nothing — it compiles, links and runs, against somebody else's code.
  Seen on 2026-09-13, where a build reported success and the new test simply was not in the
  binary. Before trusting a build in a second worktree, check where the symlink points, and
  after any `scripts/sync` assume the other worktree must sync again before it builds.
- Machine: Apple M1 Pro, 10 cores, 32 GB. Clean dev build is hours; incremental UI builds are minutes.
  Prefer the Views playground for UI iteration.
- Build configs in `build/`: `dev` (component build, minimal symbols), `perf` (release-like, for
  measurements), `release`.
- **The dev browser may be quit freely**, without asking the owner, whenever a rebuild or relaunch
  needs it. It is the build in `out/dev`, run with its own data in `Kyuzen-dev`, and the owner's
  daily browsing is in the installed release build, which has its own data and icon. Quit it by
  process, never by app name: both builds share the bundle id `io.github.tigranhov.kyuzen`, so
  "quit Kyuzen" can reach the release build. `pkill -TERM -f "out/dev/Kyuzen.app/Contents/MacOS/Kyuzen"`
  shuts it down cleanly and its tabs and spaces come back on the next launch; wait for the process
  to be gone before building. Never quit `/Applications/Kyuzen.app`.

## Commands

All scripts read `CHROMIUM_VERSION` and put depot_tools on PATH themselves. They are bash; the
interactive shell is zsh, so for ad-hoc checks use `bash -c 'source scripts/lib.sh && ...'`.

```
scripts/bootstrap             # one-time: depot_tools + shallow checkout at the pinned tag (needs git-lfs)
scripts/sync                  # idempotent: symlinks into the tree, dependency integrity check, apply patches
scripts/build <config> [tgt]  # dev | perf | release; args from build/common.gni + build/<config>.gn
scripts/run [flags] [url]     # launch out/dev with user-data-dir "Application Support/Arcium-dev"
scripts/playground            # build + launch arcium_playground, the standalone sidebar host
scripts/format [files...]     # clang-format + gn format; see below, git cl format does not work here
scripts/netaudit [seconds]    # idle network audit against docs/netaudit-allowlist.txt
scripts/perf [--runs N] [--idle S] [--label L]   # startup, idle RSS, process count -> docs/perf/
out/dev/arcium_unittests      # model and adapter tests (scripts/build dev arcium_unittests)
```

Debugging switches, passed through `scripts/run`:

```
--arcium-fake-clock-offset=13h   # only the archive service's clock moves forward
```

`--arcium-fake-clock-offset` takes a `base::TimeDeltaFromString` duration (`13h`, `2d`, `1h30m`)
and moves **only** the clock `ArchiveService` reads when it asks whether a Today tab has been idle
long enough. It deliberately does not move `base::Time::Now()` for the process: that would also
move the model store's writes, the archive rows' own timestamps and session restore's last-active
times, corrupting the very records the archive acceptance pass exists to check. An absent,
unparseable, negative or infinite value means zero. It does not override the never-archive rules,
so a pass under the switch still proves something. Declared in `arcium/common/arcium_features.h`.

Verifying UI without screen capture: `arcium_playground --snapshot=<png>` and
`scripts/run --arcium-snapshot=<png> [--arcium-snapshot-delay=<seconds>]` paint the Views tree
offscreen at 2x, write a PNG, and log every view's class, bounds and visibility. Web contents come
out blank. `--arcium-no-sidebar` runs a window without the sidebar, which is how a Chromium
regression is told apart from an Arcium one.

```
scripts/rebase <tag>          # move to a new Chromium tag, resync, reapply patches
```

Environment knobs: `ARCIUM_JOBS` (ninja parallelism, default all cores; lower it when the machine is
busy), `GCLIENT_JOBS` (default 3; higher trips Google's anonymous rate limit), `ARCIUM_CONFIG`
(dev by default), `ARCIUM_USER_DATA_DIR`, `CHROMIUM_ROOT`.

Long builds: `nohup scripts/build dev > /Volumes/Texternal/chromium/build.log 2>&1 &` and tail the log.
Siso prints dots instead of step counters under this agent environment; progress is best read by
counting `out/dev/obj/**/*.o` against the total from `ninja -C out/dev -t commands chrome | grep -ac clang`.

Reference timings on this machine (M1 Pro, USB SSD): first build 5.5 h with the machine otherwise
busy; touching one Views file and rebuilding 17 s; rebase no-op 29 s; views_examples 3 min.

## Stage status

| Stage | State |
|---|---|
| 0 Foundation | done, see docs/stage0-carryover.md and docs/perf/ |
| 1 Visual MVP | done, see docs/stage1-findings.md and docs/perf/ |
| 2 Arc tab model | done, see docs/stage2-findings.md and docs/perf/2026-09-07-stage2.md — A2.1, A2.2 and the session-restore list executed by hand 2026-09-08; four defects found and fixed during the pass |
| 2.5 Entry behaviour | done, see docs/stage2.5-findings.md and docs/perf/2026-09-09-stage2.5.md — A2.5.2 and A2.5.3 executed by hand 2026-09-09; one defect found and fixed (cold entries drew a globe instead of their stored favicon, 48f19f0) and two owner-requested changes made (depth cap 5 to 4, and "New folder" now nests where the row is, f179115); A2.5.1's collapse-then-relaunch half still NOT run, perf not yet measured |
| 2.6 Pinned and favourite home boundary | done, see docs/stage2.6-findings.md, docs/superpowers/specs/2026-09-09-stage-2.6-home-boundary-design.md and docs/research/zen-home-boundary.md — A2.6.1-A2.6.9 executed by hand 2026-09-10 against a local four-host harness, all passed, no defects found; perf not yet measured |
| 3 Spaces and profiles | 3a spaces done, see docs/stage3a-findings.md and docs/superpowers/specs/2026-09-10-stage-3a-spaces-design.md — A3a.1-A3a.8 executed by hand 2026-09-11, all passed, one defect found and fixed (the delete confirmation promised more than it could keep); perf not yet measured. R3.9 no page loads at launch done, see docs/stage3-no-load-at-launch-findings.md and docs/superpowers/specs/2026-09-11-no-load-at-launch-design.md — A3.4 and A3.9.1-A3.9.3 executed by hand 2026-09-11, all passed, no defects found; perf not yet measured. 3b profiles as storage partitions: automated and documentation pass done, see docs/stage3b-findings.md and docs/superpowers/specs/2026-09-11-stage-3b-profiles-design.md — A3b.1 (automated) green twice on `arcium_unittests` (622) and `arcium_browsertests` (31 unique), no defects found and no flakiness recurring; one deliberately-failing test rewritten against its true behaviour and one open question settled by counting (a restored tab's profile builds storage at creation but spawns no renderer process until clicked, so R3.9's process promise holds and its storage promise does not); perf measured 2026-09-12, see docs/perf/2026-09-12-stage3b.md, and contradicts two of the four expected performance answers for a profile with a restored tab; a whole-branch review afterwards found one serious defect and it is fixed with tests that fail without the fix (a model file whose profile list could not be read was refused but left in place with saving still armed, so the next change would have overwritten every space, folder and pinned entry), plus a false claim in the findings about which tab paths get fixed storage, two lesser gaps judged and recorded as accepted, and one oversized file split; a scoped re-review of those fixes approved them with no blocking findings. A3.3's idle half measured by script 2026-09-12 (three spaces across two profiles add no process and no renderer over a browser with no model at all, twice; summed RSS +19 MB, inside budget and inflated by shared-library double counting) — see the A3.3 section of docs/perf/2026-09-12-stage3b.md. The Guard and Browser-pages checks are covered by browser tests rather than by hand — the no-opener popup test asserts storage, cookie and one-tab-added, and is mutation-proven by disabling the guard's empty-tab cleanup. Seven rows were left for the owner to run by hand (A3b.2-A3b.7, A3.1, and A3.3's in-use half): they need real sign-ins and physical gestures — dragging a tab, clicking a menu item — which no agent here can produce. `scripts/acceptance-3b` sets up everything they assume (three spaces across two profiles, plus a loopback site holding two independent logins) and prints the checklist, so the pass is walking a list rather than building a world first; see docs/stage3b-acceptance-harness.md, which also states the one substitution it makes. The pass began 2026-09-13 and its first finding was in the harness, not the browser: a signed-in tab stayed parked on the sign-in address, so every reload signed it back in, making an untouched space look like it had leaked a login and a sign-out look ignored; the page now redirects to a plain address and is never cached. A3.1 and A3b.2 both passed against the corrected harness the same day: two logins held at once, each in its own space, and both survived a relaunch with nothing loading until a tab was clicked; a tab dragged between profiles read the destination's account and kept its back history, with its keeping its place attributed to `ProfileReopenTest.TheTabKeepsItsPlaceItsHandleItsEntryAndItsTag` rather than reported by hand. A3b.3 passed too — clearing one profile from the space menu left that space signed out and the shared space untouched, with the no-reload half resting on the clearing path holding no reload or navigation call rather than on the test whose address assertion cannot tell a reload apart. A3b.4 passed with nothing attributed — deleting a profile moved its space onto the shared logins, the space's tab came back by itself reading the shared account, and the deletion held across a relaunch; the reopening is right here and is what separates this row from A3b.3, since deleting destroys the storage a page was loaded in while clearing only empties it. A3b.5 passed on a clean re-run: the warning appears and says it cannot be undone, "shared logins only" leaves the separate profile signed in, "every space too" signs both out — its first attempt looked like a failure and was contaminated by a harness that served one request at a time and by the tab-move defect below. A3b.6 and A3b.7 passed the same day, both halves seen by hand: a note saved on an extension's options page from one space read back unchanged from the other, and both spaces stayed signed in across two relaunches with the extension loaded and one more after it was removed. A3.3's in-use half was the last row and is now covered by `ProfileLifecycleTest.SwitchingSpacesAddsNoProcessAndStartsNoLoad` rather than by a person watching Activity Monitor: two spaces on different profiles both signed in and showing a page, a third space on a third profile nobody has opened, nine switches between them, and the render process count unchanged with neither untouched tab starting a navigation. It carries its own positive control — it then opens a page in that third space and requires the count to rise and the space to read signed out — because everything else it asserts is that nothing happened, which is what a counter wired to nothing would also report. Green three times running. Four attempts to witness the row by hand had produced nothing at all, and it converts honestly because it asks for a count rather than a judgement. Keep the method for any row that reads as a flat line: a positive control inside the measurement is what separates a pass from an instrument that was asleep. **All seven hand rows are now done and the stage's acceptance pass is complete.** The one thing not done is a whole-suite run with the last two tests included: it was skipped at the owner's request, because browser tests take over the screen on macOS and the machine was in use. Both pass on their own and the suite passed in full just before they were added, so what is unproven is only that all 34 pass in one sitting; run it before the next stage starts. **Three defects found during the pass, two fixed and one open**: the harness was fixed twice (sign-in links reloading themselves into a second sign-in, and a single-threaded site that hung page loads); Arcium's own was that moving an open tab to a space on another profile could leave the page in a second tab, because the move reopened the tab and only re-tagged it afterwards, so the partition guard read the stale tag and relocated a tab that was already where it belonged — fixed by tagging first, with a test written first and watched to fail counting three tabs where two belonged (04e8f8c), and the clearing test's reload promise strengthened at the same time from an address it could not distinguish to a count of navigations started (ad055f1). The third is now closed too, on 2026-09-16: both delete-a-profile browser tests used to crash intermittently at shutdown on Chromium's `DCHECK failed: !storage_partition_map_`, because deleting a profile whose storage is open asks for the erase from inside the teardown that has just dropped the storage partition map, and the erase builds a new one. The erase now stops once the browser context says it is going away (`StorageCanStillBeErased`), and the storage waits for the sweep at the next launch, which the product already relied on. The twenty consecutive runs that would tell that fix from luck had never been done; twenty-six were run — four one test at a time, then twenty-two at two jobs, the condition that used to crash both on the first attempt, with the machine loaded to a 1-minute average of 101 — all green, with the check appearing nowhere. **No defect is open in this stage.** |
| 4a Address pill, extensions and the command box | built, all tests green, hand rows and perf still owed — see docs/stage4a-findings.md, docs/superpowers/specs/2026-09-13-stage-4a-pill-and-extensions-design.md and docs/superpowers/plans/2026-09-14-stage-4a-pill-and-extensions.md. Pulled forward out of Stage 4 so the browser could be used for real work: extensions installed with nowhere to appear, and the address bar in the sidebar looked wrong. 642 unit tests pass. **The browser tests ran for the first time on 2026-09-15**, seven of them red, and all seven are now fixed. Two were defects in this stage's own code, both of which a hand pass would have hit: clicking the pill focused the hidden address bar behind it rather than opening the box, because that bar's address field was taking the click (it now takes no events while it is silent, and takes them again while a permission question is up); and the extensions strip's own menu button sat in the sidebar row, because auto-hide mode only drops buttons under squeeze and a row of its own never squeezes, so an empty row was a 26px gap with a second way into the menu in it (the row now places that button where its own edge cuts it off, and hands the strip the width it needs even at zero height -- hiding the button outright makes the strip hide the pinned ones too). The other five were defects in the tests: the pill shows a port and the test server picks a new one each run; a permission request without a user gesture is a bubble, not the chip the test is about; "a tab you already have" needs a second tab, or the page is only in history and the wait hangs until the launcher kills it (that wait is now bounded and fails honestly); visiting a page to write history leaves a tab on it, and a tab outranks both things that test compares; and a newly pinned extension button fades in, so the test must let the fade finish. All 63 browser tests and 675 unit tests pass. The seven hand rows in scripts/acceptance-4a have not been run and perf has not been measured. The central finding: Chromium's LocationBarView cannot be removed from the pill, because password and page-information bubbles anchor through it and a permission request is a chip it owns, so it stays drawn and silent with the pill drawn over it |
| 4b Peek, the outside-link window, routing rules and box commands | built, green, and two-thirds executed by hand — see docs/stage4b-findings.md and docs/superpowers/specs/2026-09-15-stage-4b-navigation-design.md. Covers R4.1's commands-in-the-box half, R4.3 (the small window a link from another application opens in), R4.4 (peek), R4.5 (routing rules) and R4.6 (site search). Written in one pass while a forced full rebuild held the checkout, at the owner's request, with every Chromium API checked by reading headers; built the same day. **675 unit tests and all 63 browser tests pass**, the 13 new ones among them; four defects were found by that first build and run and are fixed (two dead accessors in the small window, a missing profile declaration in three tests, a third command that answers to "clone", and seven menu expectations). **No acceptance row has been executed by hand and perf is not measured.** One new patch, `0220-outside-links-mac.patch`, and no GN wiring with it. Unit tests written for the routing rules, the serialiser and its version 4 to 5 migration, command matching, the row menu and the loose-page marker; browser tests written for peek, outside links and the box's routing and commands. The hand pass began 2026-09-16: twenty of the 34 rows in docs/stage4b-hand-checks.md were driven against a running browser from this machine's own keyboard and pointer — the sidebar-to-page edge, the outside-link window in all four of its behaviours, a local file declining that window, a site routed to a space and unrouted again, and five of the six command rows — and all twenty passed, including the rule surviving a quit and relaunch and the small window naming the rule's space rather than the space on screen. **Three defects were found outside the rows and all three are fixed, each with a test that fails without the fix**: the blank tab a window lands on in an empty space read Chromium's "Untitled", which names a page that failed to name itself rather than a tab that has been nowhere, and now reads "New tab"; the pill was empty on a local file and on a settings page, though the Stage 4a design promised a short label there and a unit test had frozen the emptiness as the rule, so `PillDomain` became `PillLabel` and names the file, says `chrome://settings`, or says "New tab"; and launching the browser while it was already running opened a second window, which one window holding one strip cannot afford, so the launch now raises the window that exists and opens any address on its command line as a tab in it, routed the same way the box routes one, declining a private window, an installed web app and another profile — a second patch, `0230-second-launch-one-window.patch`, hooked at `StartupBrowserCreator::ProcessCommandLineAlreadyRunning`, and its test goes through that entry point rather than the function the hook calls, because a test of the function alone would pass with the hook unapplied. 679 unit tests and all 67 browser tests pass. The pass continued 2026-09-20 with the peek rows, the close-a-tab row and the four site search rows, all driven by hand and all passed, which leaves only a link clicked in another application and the Cmd+W question. **One more defect was found and fixed**: the peek drew its card and nothing else, because the page underneath draws through a compositor layer and a view without one paints beneath every layer inside its parent's -- so the dimming and the close and open-as-tab buttons were invisible, leaving a peek with no visible way out. The peek now paints to a layer of its own, stacked above the rest, with a test that fails without the fix (c15175d); the peek rows were then walked a second time and the dimming and the buttons seen by eye. 679 unit tests and all 68 browser tests pass in one sitting, which also settles the whole-suite run the Stage 3b row was still owed. A second defect was found in row 26 and fixed the same day: the box's rows answered the keyboard only, so nothing happened under the pointer and a click on a row did nothing at all, while plain Chromium's list (checked through `--arcium-no-sidebar`) does both. Rows now mark themselves under the pointer and take themselves on a click, the row Enter would take keeping the stronger mark; writing the test caught a second thing, that the clicked row must be copied rather than remembered by its number, because a new set of answers renumbers the rows under a click that has already happened (d1ce5dc). Both tests were watched to fail against a mutated row, and the first version of the hover test passed under that mutation because it held row-view pointers across a run loop that had already destroyed them -- it now asks the box for each row and spins no loop. 679 unit tests and all 70 browser tests pass. What is left: hand rows 10 and 15, the seven Stage 4a rows, and perf |
| 4 Command bar and navigation | not started; R4.7 (the pill) and R4.8 (extension homes) are done in 4a above, and bookmarking is dropped permanently at the owner's decision — spaces and pinned tabs replace it |
| 5a Split view | built and green, hand rows still owed — see docs/stage5a-findings.md, docs/superpowers/specs/2026-09-22-stage-5a-split-view-design.md and docs/superpowers/plans/2026-09-22-stage-5a-split-view.md. Covers R5.1. Chromium's split is graduated and carries no feature flag, so the panes, the divider, the resize and the mini strip were not rebuilt; what Arcium lacked was every way in, because Chrome's entry points live in the tab strip this browser hides. Four were added — a sidebar row dragged onto the page, the row menu, Cmd+Option+S, and a "Split the screen" command in the box — plus one rule about who may share a screen (never two spaces, never a loose page, never a tab already split), an end-the-split-first step before either half leaves its space, and a record in the model so a split survives a quit (the two tabs' TabKeys, the layout and the ratio; model schema 5 to 6 with a no-op migration). No patch was added. Written in one pass with no compile while the machine was in use, at the owner's request; five defects were found by reading in that pass, the worst being a controller constructor that registered with nothing and left the shortcut and the whole of the persistence dead (b949f8c). **Built and run for the first time on 2026-09-22**, which found seven more, all fixed: two compile failures, one product defect (the box's "which tab" question was overwritten by late suggestions for "split", so Enter took a history row — 798266f), and four test defects, one of them the window drag test's full-screen case from earlier the same day, which waited on a real macOS animation that never finishes under test (f3170c8). 741 unit tests and all 87 browser tests pass; the browser suite passed in one sitting three times, two of them needing one retry each — two box split tests once, closed right after Enter under a ten-browser start with the cause unknown and now self-reporting (be48a98), and Stage 4a's pill permission test once. Perf 2026-09-22 (docs/perf/2026-09-22-stage5a.md): 9 processes and 1464 MB idle against Stage 4's 9 and 1459, so no process and no idle memory added; startup 2927 ms on a quiet machine. scripts/perf had been broken by the rename to Kyuzen and is fixed. Two panes rather than R5.1's two to four, recorded as deviation D5-1 in section 7 of the master spec. **What is owed: the ten hand rows in docs/stage5a-hand-checks.md.** R5.2 to R5.4 not started |
| 5b A split as one sidebar entry | built and green, hand rows and perf still owed — see docs/stage5b-findings.md, docs/superpowers/specs/2026-09-23-stage-5b-split-entry-design.md and docs/stage5b-hand-checks.md. Answers the 5a hand pass. Dragging a row now opens an 80px strip beside the page to drop on, because the page swallows drags on macOS (1e5d0ae). A split draws as one row with its halves side by side; two pinned entries sharing the screen are linked in the model (schema 7) and move, file, pin, unpin and relaunch as one entry; ending the split unlinks them and closing a tab does not (f9b06b6). **One older defect found and fixed**: a pinned tab came back cold after a relaunch, its page a second time in Today, because restore asked for the entry before the model file had loaded; the bind now waits for the load (666f923). 773 unit tests pass, as do the 11 split browser tests and 40 restore, profile and box-command browser tests; whole browser suite not run since. **All eight hand rows passed on 2026-09-23.** Two findings open: the page behind the split divider lags on heavy pages (the divider is Chromium's, not yet compared with plain Chromium), and a pinned row cannot be dragged to the very bottom of Pinned. Owed: perf, a whole-suite browser run |
| 6 Customisation and library | not started |
| 7 Performance and lifecycle | not started |
| 8 Distribution | not started |
