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

- Chromium C++ style, enforced with `git cl format` / clang-format from the checkout.
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

Filled in during Stage 0. Until then, nothing here is runnable.

```
scripts/sync            # fetch the pinned tag, apply patches, refresh symlink
scripts/build <config>  # dev | perf | release
scripts/run             # launch the dev build with a scratch user-data-dir
scripts/rebase <tag>    # move CHROMIUM_VERSION, reapply patches, report conflicts
scripts/perf            # startup, idle memory, process count vs vanilla; writes docs/perf/
```

## Stage status

| Stage | State |
|---|---|
| 0 Foundation | not started |
| 1 Visual MVP | not started |
| 2 Arc tab model | not started |
| 3 Spaces and profiles | not started |
| 4 Command bar and navigation | not started |
| 5 Layout | not started |
| 6 Customisation and library | not started |
| 7 Performance and lifecycle | not started |
| 8 Distribution | not started |
