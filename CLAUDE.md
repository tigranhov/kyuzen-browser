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
Chromium keys by file name.

### Development workflow

1. Brainstorm and get approval before implementing (superpowers:brainstorming). No code before a yes.
2. Each stage gets a plan in `docs/superpowers/plans/` (superpowers:writing-plans) derived from the
   stage's requirements in the spec. Execute plans task by task.
3. Test-driven: unit tests in `arcium/test/` for models and services, browser tests for tab-to-space
   mapping, partition isolation and persistence. Write the failing test first.
4. Iterate on Views UI in the standalone Views playground first, then wire into the browser.
5. Verify before claiming done: build passes, tests pass, the stage's acceptance list executed by
   hand, perf result recorded. Report failures with output, not summaries.
6. Commit small. Commit messages say why. Do not commit Chromium sources or build output.

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
- Machine: Apple M1 Pro, 10 cores, 32 GB. Clean dev build is hours; incremental UI builds are minutes.
  Prefer the Views playground for UI iteration.
- Build configs in `build/`: `dev` (component build, minimal symbols), `perf` (release-like, for
  measurements), `release`.

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
| 3 Spaces and profiles | 3a spaces done, see docs/stage3a-findings.md and docs/superpowers/specs/2026-09-10-stage-3a-spaces-design.md — A3a.1-A3a.8 executed by hand 2026-09-11, all passed, one defect found and fixed (the delete confirmation promised more than it could keep); perf not yet measured. R3.9 no page loads at launch done, see docs/stage3-no-load-at-launch-findings.md and docs/superpowers/specs/2026-09-11-no-load-at-launch-design.md — A3.4 and A3.9.1-A3.9.3 executed by hand 2026-09-11, all passed, no defects found; perf not yet measured. 3b profiles as storage partitions: automated and documentation pass done, see docs/stage3b-findings.md and docs/superpowers/specs/2026-09-11-stage-3b-profiles-design.md — A3b.1 (automated) green twice on `arcium_unittests` (622) and `arcium_browsertests` (31 unique), no defects found and no flakiness recurring; one deliberately-failing test rewritten against its true behaviour and one open question settled by counting (a restored tab's profile builds storage at creation but spawns no renderer process until clicked, so R3.9's process promise holds and its storage promise does not); perf measured 2026-09-12, see docs/perf/2026-09-12-stage3b.md, and contradicts two of the four expected performance answers for a profile with a restored tab; a whole-branch review afterwards found one serious defect and it is fixed with tests that fail without the fix (a model file whose profile list could not be read was refused but left in place with saving still armed, so the next change would have overwritten every space, folder and pinned entry), plus a false claim in the findings about which tab paths get fixed storage, two lesser gaps judged and recorded as accepted, and one oversized file split; a scoped re-review of those fixes approved them with no blocking findings. A3.3's idle half measured by script 2026-09-12 (three spaces across two profiles add no process and no renderer over a browser with no model at all, twice; summed RSS +19 MB, inside budget and inflated by shared-library double counting) — see the A3.3 section of docs/perf/2026-09-12-stage3b.md. The Guard and Browser-pages checks are covered by browser tests rather than by hand — the no-opener popup test asserts storage, cookie and one-tab-added, and is mutation-proven by disabling the guard's empty-tab cleanup. Seven rows were left for the owner to run by hand (A3b.2-A3b.7, A3.1, and A3.3's in-use half): they need real sign-ins and physical gestures — dragging a tab, clicking a menu item — which no agent here can produce. `scripts/acceptance-3b` sets up everything they assume (three spaces across two profiles, plus a loopback site holding two independent logins) and prints the checklist, so the pass is walking a list rather than building a world first; see docs/stage3b-acceptance-harness.md, which also states the one substitution it makes. The pass began 2026-09-13 and its first finding was in the harness, not the browser: a signed-in tab stayed parked on the sign-in address, so every reload signed it back in, making an untouched space look like it had leaked a login and a sign-out look ignored; the page now redirects to a plain address and is never cached. A3.1 and A3b.2 both passed against the corrected harness the same day: two logins held at once, each in its own space, and both survived a relaunch with nothing loading until a tab was clicked; a tab dragged between profiles read the destination's account and kept its back history, with its keeping its place attributed to `ProfileReopenTest.TheTabKeepsItsPlaceItsHandleItsEntryAndItsTag` rather than reported by hand. A3b.3 passed too — clearing one profile from the space menu left that space signed out and the shared space untouched, with the no-reload half resting on the clearing path holding no reload or navigation call rather than on the test whose address assertion cannot tell a reload apart. A3b.4 passed with nothing attributed — deleting a profile moved its space onto the shared logins, the space's tab came back by itself reading the shared account, and the deletion held across a relaunch; the reopening is right here and is what separates this row from A3b.3, since deleting destroys the storage a page was loaded in while clearing only empties it. A3b.5 passed on a clean re-run: the warning appears and says it cannot be undone, "shared logins only" leaves the separate profile signed in, "every space too" signs both out — its first attempt looked like a failure and was contaminated by a harness that served one request at a time and by the tab-move defect below. A3b.6 and A3b.7 passed the same day, both halves seen by hand: a note saved on an extension's options page from one space read back unchanged from the other, and both spaces stayed signed in across two relaunches with the extension loaded and one more after it was removed. One row left, A3.3's in-use half, and it was attempted twice by sampling the browser's process makeup once a second for three minutes rather than by asking the owner to read Activity Monitor. Both runs were flat — 12 processes, 6 of them drawing pages, never varying — which is what a pass looks like and also what an untouched browser looks like; the second run carried a positive control (click one unloaded tab near the end, which must add a process) and it never fired, so the runs read as nobody being at the machine and the row stays unrun. Keep the method: a positive control inside the measurement is what turns a flat line into evidence. **Three defects found during the pass, two fixed and one open**: the harness was fixed twice (sign-in links reloading themselves into a second sign-in, and a single-threaded site that hung page loads); Arcium's own was that moving an open tab to a space on another profile could leave the page in a second tab, because the move reopened the tab and only re-tagged it afterwards, so the partition guard read the stale tag and relocated a tab that was already where it belonged — fixed by tagging first, with a test written first and watched to fail counting three tabs where two belonged (04e8f8c), and the clearing test's reload promise strengthened at the same time from an address it could not distinguish to a count of navigations started (ad055f1). The open one is older than this pass and was misattributed here before: both delete-a-profile browser tests crash intermittently at shutdown on Chromium's `DCHECK failed: !storage_partition_map_`, and it is not the parallel-load crash it was recorded as — it reproduces one test at a time, and six runs with the tab-move fix put aside and rebuilt without it produced one crash, so it predates that fix. Deleting a profile starts an erase that outlives the call, and a browser context torn down mid-erase trips the check; the next launch's sweep erases storage belonging to no profile, so the cost is disk until relaunch rather than a deleted profile's cookies staying readable. Left open deliberately and worth picking up before the next stage |
| 4 Command bar and navigation | not started |
| 5 Layout | not started |
| 6 Customisation and library | not started |
| 7 Performance and lifecycle | not started |
| 8 Distribution | not started |
