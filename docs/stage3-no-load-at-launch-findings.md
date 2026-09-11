# Stage 3 — No page loads at launch: findings

After a restart only the tab on screen loads. Everything else that comes back
keeps its title, favicon and back history and waits to be clicked. Five planned
tasks, executed subagent-driven with a fresh implementer per task and a review
between, then one whole-branch review and a hand pass. What follows is what the
piece cost and what it taught, not a summary of the diff.

## What shipped

**One hook at one handoff.** Session restore loads the tab on screen itself and
hands every other restored tab to Chromium's background loader, which starts at
least four and up to twenty pages nobody asked for. `patches/0170` asks Arcium
first at that one call. Arcium keeps the one useful thing the loader did for a
tab it had not reached — the favicon lookup — and loads nothing.
Cmd+Shift+T reaches the same loader from a different call site and is left
alone, because those tabs were asked for.

**One definition of "not loaded".** `arcium::IsTabUnloaded` is true when a tab's
controller needs a reload or the tab was discarded. A restored tab that has not
loaded is the first; a tab Chromium dropped to save memory is the second. Both
mean the same thing to the user: clicking this costs a page load.

**The row asks once, the views ask once.** A sidebar row gains `is_unloaded`,
set only for a live, non-active tab, and `needs_load()`, which is true for a
closed entry or an unloaded tab. Both views dim on that one question, using the
same colour and opacity a closed row already used. Nothing is stored, counted,
timed or polled: the sidebar already rebuilds on every tab-strip change, and a
tab that starts loading is such a change.

**A feature flag, on by default.** `--disable-features=ArciumNoLoadAtLaunch`
brings Chromium's background loading back for a side-by-side comparison.

## What the branch taught

**The favicon fetch was load-bearing.** The background loader fetched each
tab's favicon before queueing it, which is why a restored session does not come
back as a wall of globes. Skipping the loader without keeping that call would
have traded one visible regression for one invisible improvement.

**One notification stops firing.** The skipped call is the only caller of
session restore's "finished loading tabs" notification, so it never fires and
restore stays "in progress" for the life of the process. Nothing on macOS
listens: the sole observer is a ChromeOS throttle, the tab manager declares the
method without registering it, and the startup reference needs a flag that is
off by default and expires by itself. Recorded here because a rebase will meet
it again.

**Turning the rule off does not turn the dimming off.** The dimming answers
"will clicking this load a page", which stays true of a discarded tab whatever
the restore rule says. So a comparison run shows dimmed rows for tabs the
loader has not reached yet. Deliberate, and now written into the design.

**A test can prove the answer without proving the redraw.** The tests pin what
a row computes, not that the sidebar is told to redraw when a tab starts
loading. That path is Chromium's own synchronous tab-strip update, read at
review time and confirmed by hand, not by a test. Arcium has no whole-browser
tests yet; this is the second stage to note the same gap.

## The hand pass

Executed by the owner on 2026-09-11 against A3.4 and A3.9.1–3: pinned and Today
tabs across two spaces, quit and relaunch, the task manager's renderer count, a
pin left away from its home page, Cmd+Shift+T, and a comparison run with the
flag off. All passed. No defects found.

## Still open

- Perf not yet measured for this piece or for Stage 3a.
- A third look for unloaded tabs, if R7.5 ever wants one (D3-2).
- A settings switch for the dimming, deferred to Stage 6 along with the
  closed-row switch.
- Keeping a reopened window's background tabs unloaded, deliberately out of
  scope: those tabs were asked for.
