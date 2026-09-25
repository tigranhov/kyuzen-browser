# Welcome and Zen/Arc import

Design: `docs/superpowers/specs/2026-09-24-welcome-and-import-design.md`, with
its mockups in `docs/superpowers/specs/2026-09-24-welcome-mockups/`. Plan:
`docs/superpowers/plans/2026-09-24-welcome-and-import.md`. File formats:
`docs/research/zen-arc-import-formats.md`. Hand rows:
`docs/welcome-hand-checks.md`.

## Where it stands

**Built and green; the hand rows and perf are still owed.** Every unit test
and browser test this work touches passes: the importer's 64, the model's
new facts, the card's 14, the browser side's 12 helpers and the command
box's, and 11 welcome browser tests (two of them relaunch pairs) plus the 16
command box browser tests the new command reaches. Neither whole suite has
been run, because the owner has not said the machine is free.

Each part was also checked by breaking it on purpose and watching for a test
to fail: 30 breakages of the importer and of what the model knows, 14 of
the card and 15 of the browser side. Three went unnoticed at first. Two were
the importer's: one showed a guard that could never matter, which was
removed, and one a rule with no test, which got one. The third was the
browser side's: an import that no longer moved the window to the first space
that arrived passed, because on a fresh install removing the empty starter
moves the window there anyway. A test of the command box's import on a setup
already in use now covers it and catches that breakage.

## What was built

**The importer** (`f3be888`). Readers for Zen's `zen-sessions.jsonlz4`
(with its own mozLz4 decoder) and Arc's `StorableSidebar.json`, a finder that
looks for both under a home folder, and an applier that brings a plan into
the model: spaces, profiles for spaces that kept their logins apart,
favourites, pinned tabs and folders, all cold. A second import adds only what
is missing.

**What the model knows** (`a0fc77e`). Whether this launch found no model file,
which is what a fresh install is; a callback once loading ends; and a pref
holding the welcome's step.

**The card** (`bd622aa`). Six steps over the page area with the sidebar
beside it, drawn from the approved mockups with the agreed changes. It knows
nothing of the browser and runs in the playground on a fake.

**The browser side** (`183a6e9`). The card shows when a fresh install's
model has loaded, or at the step a quit left it on, and never for someone
updating. It looks for Zen and Arc on the thread pool once the card is up,
imports into this profile's model, moves the window to the first space that
arrived and removes the empty one a fresh install starts with, offers
Chromium's engines for this country, asks macOS to make Kyuzen the default,
and stores each step reached. The command box's "Import from Zen or Arc"
opens the first step alone.

Two switches: `--arcium-welcome` lets the welcome follow its rule under
`--no-first-run`, which every browser test and `scripts/run` pass, and
`--arcium-import-home=<folder>` stands in for the home folder, so no test and
no hand check has to read the machine's own browsers.

## Defects found on the way, all fixed

- **Closing a window with the card up crashed.** A window deletes its child
  views before it lets go of the sidebar controller, so the card was gone
  when the welcome went to remove it. The welcome now tracks the card rather
  than holding it.
- **The search step never ticked the engine in use.** Chromium's "can this
  be made the default" says no to the engine that already is, so the list
  left it out; and the default is kept as a copy, so it never matched by
  object either. The list keeps every prepopulated engine and matches by
  Chromium's number for it.
- **The last step's tip "⌘S show or hide the sidebar" was not true of
  Kyuzen**, where Cmd+S saves the page. It became "⌃1 jump to a space by its
  place in the bar", which is.
- **Text on the scrolling choices tripped Chromium's check** that crisp text
  sits on something opaque, because the scroller drew on a clear layer of its
  own. It now paints the card's colour.

## Found, not fixed: the peek has the same close crash

The peek removes its view from the window the same way the welcome did, so
closing a window with a peek open should crash the same way. It predates this
work and is reported here rather than changed.

## Known limits

- **Going back after the import and changing a choice adds only what is
  missing.** Turning separate logins on for a space that already arrived does
  not move it onto a profile of its own.
- **An unreadable file does nothing visible.** Picking a file that is neither
  Zen's nor Arc's leaves the step as it was; there is no message yet.
- **Relaunching on step 2, before the import ran, starts from the default
  choice again**, whatever was chosen on step 1 before the quit.
- **A second window opened during the welcome shows its own card**, at the
  step stored.
- **The dev browser needs `--arcium-welcome`** to show it, since
  `scripts/run` passes `--no-first-run`; see the hand checks.

## Performance

1. **Processes:** none added. The card is Views in the browser process and
   the readers run on the thread pool.
2. **Idle memory:** none once it ends. The card and its controller exist only
   while shown; a finished welcome leaves one pref value.
3. **Startup:** nothing before first paint. The decision waits for the model
   load that already happens, and looking for Zen and Arc starts after the
   card is up.
4. **UI thread:** reading, decompressing and parsing are off it. Applying a
   plan is model calls on it, one redraw and one delayed write.

Not measured: `scripts/perf` waits for the owner to say the machine is free.
