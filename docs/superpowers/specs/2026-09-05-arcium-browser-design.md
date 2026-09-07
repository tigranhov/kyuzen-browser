# Arcium Browser: Master Design and Stage Requirements

Date: 2026-09-05
Status: Approved 2026-09-05
Scope: The whole product. Each stage below becomes its own implementation plan in `docs/superpowers/plans/`.

## 1. Vision

Arcium is a Chromium-based desktop browser that reproduces the Arc and Zen browsing model: a vertical sidebar with Favorites, Pinned and Today tabs, Spaces bound to isolated Profiles, a unified command bar, split view, Little Arc, Peek, and per-site customisation. It must be a complete browser on day one (extensions, passwords, autofill, PDF, DevTools, media) and it must be lighter and faster than Chrome, not heavier.

Arc was discontinued. Zen is Firefox-based. Arcium fills the gap on Chromium.

## 2. Decisions already made

| # | Decision | Choice | Why |
|---|---|---|---|
| D1 | Engine integration | Full Chromium fork with an overlay of our code plus a minimal patch series | Only path to a fully functional browser with no extra layers. Arc, Brave, Vivaldi, Helium prove the model |
| D2 | Always-visible UI | Chromium Views (C++), never WebUI | Zero extra processes, no IPC on interaction, cross-platform for free |
| D3 | On-demand UI | WebUI allowed for rarely-open, document-like surfaces: settings, library, new tab page | Right tool for rich content; process cost only while open |
| D4 | Platform | macOS first, cross-platform by construction. Zero Swift or AppKit UI code | Views already runs on Windows and Linux; Arc's Swift shell cost them a two-year port |
| D5 | Profiles per Space | Storage partitions inside one Chromium profile (Firefox containers model, as Zen does) | Separate sign-ins in one window, light: extensions, history, passwords loaded once |
| D6 | Google services | Remove Google sign-in, Chrome Sync, metrics and crash reporting to Google, promos, Finch. Keep Safe Browsing (toggle), component updater, Chrome Web Store | Startup, background network and memory. Web Store must keep working |
| D7 | Upstream cadence | Track Chromium stable, rebase every release (4 weeks) | Stay on the security train |
| D8 | Out of MVP | Sync and accounts, AI features, Easels and Notes, mobile, Windows and Linux builds | Not needed to validate the product; none requires redesign later |

## 3. Non-functional requirements (apply to every stage)

These are measured against a vanilla Chromium build of the same revision, same build flags, on the same machine. Measurements are scripted (Stage 0) and re-run at the end of each stage.

The numbers below are provisional placeholders. They are calibrated after Stage 0 produces a baseline, and until then they are advisory: a perf result is recorded and discussed, it does not block a stage. The development machine is shared and often loaded, so perf runs are taken when it is quiet and the perf script must be quick to run and quick to skip.

| ID | Requirement | Budget |
|---|---|---|
| NF1 | Extra processes for the browser UI | 0 while no WebUI surface is open |
| NF2 | Idle memory, one window, five tabs, after 60 s | vanilla + 20 MB max |
| NF3 | Cold start to first window painted | vanilla + 50 ms max |
| NF4 | Sidebar interaction (click, hover, switch space) UI-thread work | under 8 ms per frame; animations hold 60 fps |
| NF5 | UI-thread blocking | No synchronous disk or network I/O from any UI code. Persistence writes go through `ImportantFileWriter` or a background sequence |
| NF6 | Per-tab overhead added by Arcium | 0 extra processes, under 1 KB of browser-side state per tab beyond Chromium's own |
| NF7 | Background spaces | Must not add work: no timers, no polling, no thumbnails unless visible |
| NF8 | Every new feature | Answers the four questions in CLAUDE.md before it is built. This one is not provisional |

## 4. Architecture

### 4.1 Repository and source layout

Chromium is too large to live in this repo. Layout:

```
/Volumes/Texternal/chromium/src        # upstream checkout, pinned tag, patches applied
/Volumes/Texternal/repositories/arcium # this repo
  CLAUDE.md
  CHROMIUM_VERSION                     # the pinned upstream tag
  arcium/                              # ALL our code. Symlinked to chromium/src/arcium
    browser/                           # models, services, persistence
    ui/                                # Views: sidebar, command bar, split view
    webui/                             # on-demand WebUI surfaces
    common/                            # shared constants, prefs, feature flags
    test/                              # unit and browser tests
  patches/                             # ordered series: NNNN-short-name.patch
  build/                               # args.gn templates: dev, perf, release
  scripts/                             # sync, apply-patches, build, run, rebase, perf
  docs/superpowers/specs/              # this document and per-feature specs
  docs/superpowers/plans/              # per-stage implementation plans
```

Rules: upstream files are touched only by patches. A patch is a hook that delegates into `arcium/`; logic never lives in a patch. Each patch is small, single-purpose, and carries a header explaining why it exists. Rebases are then mechanical.

### 4.2 Core objects

- **Space.** `id, name, icon, gradient, profile_id, order, last_active_tab`. A window shows one space at a time.
- **Profile (Arcium profile).** `id, name, colour, partition_id`. Maps to a `content::StoragePartitionConfig` inside the single Chromium profile. The default profile maps to the default partition.
- **Favorite.** `id, space_id, home_url, title, icon, order`. Always visible in the grid, survives restarts, keeps a live tab only while open.
- **Pinned tab.** `id, space_id, pinned_url, title, folder_id, order`. Persistent. Navigating away is allowed; "return to pinned URL" restores it.
- **Today tab.** A live tab that is neither favorite nor pinned. `tab_id, space_id, last_active`. Auto-archived after the space's idle timeout.
- **Folder.** `id, space_id, name, collapsed, order`. Contains pinned tabs. Built on Chromium's tab group data where it fits.
- **Archive entry.** `url, title, space_id, archived_at`. Restorable from the Library.

### 4.3 How tabs map to spaces

One window, one `Browser`, one `TabStripModel`, as Chromium expects. Arcium keeps a side table `tab -> space`. The sidebar shows only the active space's tabs; other spaces' tabs stay in the model, hidden, and are subject to the unloading policy (Stage 7). Switching space activates that space's `last_active_tab`. New tabs and opened links inherit the space and profile of the tab that spawned them.

### 4.4 How profiles isolate

Each Arcium profile owns a `StoragePartitionConfig`. A tab in that profile is created with a `SiteInstance` fixed to that partition, so cookies, local storage, IndexedDB, cache and service workers are separate, and renderer processes are never shared across partitions. Popups and child frames inherit the partition. Extensions, history, bookmarks and passwords are shared across profiles by design.

Known limits, accepted: extensions that manage cookies see only the default partition; Chrome's built-in site-data settings show only the default partition, so Arcium ships its own per-profile clear-data control.

### 4.5 Persistence

Live model (spaces, profiles, favorites, pinned, folders) is one JSON file per Chromium profile, written atomically via `ImportantFileWriter`, following the Bookmarks model precedent. The archive is SQLite via Chromium's `sql::Database`, since it grows without bound. Nothing is written synchronously on the UI thread.

### 4.6 UI composition

The native tab strip and toolbar are hidden. `BrowserView` gains a left `SidebarView` and an inset, rounded `ContentsContainer` on a gradient background. Everything in the sidebar is Views. The command bar is a Views bubble that reuses the omnibox model for suggestions. WebUI surfaces open in a tab or a side panel on demand.

## 5. Stage requirements

Each stage has a goal, numbered requirements (R), and an acceptance list (A) that a person can verify by hand. Requirements are the contract for that stage's implementation plan.

### Stage 0. Foundation

Goal: a branded, de-Googled Chromium that builds, runs and is measurable, with the infrastructure that makes every later stage cheap.

- R0.1 depot_tools installed; Chromium checked out at the current stable tag on the external drive; `CHROMIUM_VERSION` records the tag.
- R0.2 Repo layout from 4.1 exists. `chromium/src/arcium` symlinks to `arcium/`.
- R0.3 Scripts: `sync` (fetch tag, apply patches), `build` (dev, perf, release configs), `run`, `rebase` (new tag, reapply, report conflicts), `perf` (startup, idle memory, process count against vanilla).
- R0.4 Dev build config uses component build and minimal symbols; a clean dev build completes; incremental UI change builds in minutes.
- R0.5 Standalone Views playground target builds and can host Arcium sidebar components.
- R0.6 Branding: product name Arcium, bundle id, icon, user data directory.
- R0.7 Google sign-in, Sync, UMA metrics, crash reporting to Google, promos and Finch are compiled out or disabled. Safe Browsing remains a toggle.
- R0.8 Chrome Web Store installs an extension successfully.
- R0.9 Baseline perf numbers for vanilla and Arcium are recorded in `docs/perf/`.

Acceptance:
- A0.1 Arcium.app launches, browses, plays video, opens DevTools, installs an extension.
- A0.2 Running the perf script prints startup, idle memory and process count for vanilla and Arcium within NF budgets.
- A0.3 Rebase script on the same tag is a no-op with zero conflicts.

### Stage 1. Visual MVP, the iteration start

Detailed design: `2026-09-06-stage-1-visual-mvp-design.md` (visual decisions, components, hooks).

Goal: the complete Arc layout on screen, bound to live tabs, usable as a daily browser, so look and feel can be judged and redirected before deep features.

- R1.1 Native horizontal tab strip and top toolbar hidden.
- R1.2 `SidebarView` on the left with all zones present: traffic-light area, back and forward, URL pill, Favorites grid, Pinned section, divider, Today section, new tab row, bottom bar with space dots and a menu button.
- R1.3 Sidebar reflects the live `TabStripModel`: titles, favicons, loading throbber, audio indicator, active highlight.
- R1.4 Click selects a tab; hover shows a close button; Cmd+T adds a tab in Today; drag reorders within Today.
- R1.5 URL pill shows the active tab's URL; clicking it opens the omnibox for editing.
- R1.6 Content area inset with rounded corners on a gradient background; sidebar width fixed for this stage.
- R1.7 One default space and one default profile. Favorites and Pinned sections render but may hold only in-session items.
- R1.8 All Chromium keyboard shortcuts keep working. Session restore keeps working.
- R1.9 Every sidebar component is also hosted in the Views playground.

Acceptance:
- A1.1 Open Arcium, browse for a day: switching, closing, opening tabs all from the sidebar.
- A1.2 Screenshot side by side with Arc: same zones, same layout order.
- A1.3 Perf script within NF budgets; process count unchanged from vanilla.

### Stage 2. The Arc tab model

Goal: Favorites, Pinned and Today behave like Arc, and survive restarts.

- R2.1 Drag a tab into the Favorites grid makes a Favorite; grid persists; a Favorite opens its `home_url` if no live tab, otherwise focuses it.
- R2.2 Drag a tab above the divider pins it; pinned tabs persist; "return to pinned URL" available on hover and by shortcut when the tab has navigated away.
- R2.3 Today tabs auto-archive after a per-space idle timeout (12 h, 24 h, 7 d, never); "Clear" archives all Today tabs.
- R2.4 Tab rename inline; renamed title persists for pinned and favorites.
- R2.5 Folders for pinned tabs: create, rename, collapse, drag in and out.
- R2.6 Tab search across all spaces by title and URL.
- R2.7 Persistence per 4.5, atomic and off the UI thread.

Acceptance:
- A2.1 Pin, favorite, rename, fold; quit and relaunch; everything is where it was.
- A2.2 Set 12 h timeout, fake the clock, Today tabs archive and appear in the archive list.
- A2.3 Perf within budgets; no writes on the UI thread confirmed by the tracing script.

### Stage 2.5. Entry behaviour

Goal: a favourite is a place you keep, not just a tab that persists; a folder can
hold a folder.

Added after Stage 2 shipped. Neither requirement below was a deliberate omission
— the spec simply never said anything about them, and building Stage 2 made both
absences visible. R2.5's folders are flat because line 76 defines a Folder as
containing pinned tabs and gives it no parent; favourites navigate anywhere
because the spec grants that to pinned tabs explicitly (line 74) and is silent
for favourites, so they inherited the silence.

- R2.5.1 Folders nest. A `Folder` gains a parent, a folder can be dragged into
  another folder, and a collapsed folder hides its descendants. Dragging a
  folder into its own descendant is refused rather than accepted into a cycle.
  Whether a folder's count includes descendants is a display decision to settle
  in its plan, not a model one.
- R2.5.2 The live model file gains a schema migration. It runs once, off the UI
  thread, and a file it cannot migrate degrades to an empty model rather than
  losing the archive — the same rule Stage 2 already applies to a corrupt file.

Scheduled before Stage 3 deliberately: Stage 3 extends the same JSON schema with
a real space dimension, and migrating the model file once is cheaper and safer
than migrating it twice.

Acceptance:
- A2.5.1 Build a folder three deep, collapse the middle one, quit and relaunch;
  the tree and its collapsed state are where they were.
- A2.5.2 Drag a folder onto its own child; the drop is refused and the model is
  unchanged.
- A2.5.3 A Stage 2 model file opens in a Stage 2.5 build with every folder and
  entry intact.

### Stage 2.6. Favourite home boundary

Goal: navigating a favourite's tab away from its home does not consume the
favourite — it opens a tab instead, the way Arc and Zen behave.

**Design pending.** This stage has a goal and no requirements on purpose. The
mechanism is straightforward — a navigation seam behind a patch, delegating to
`arcium/` — but the mechanism is not the feature. The policy is: which
navigations count as leaving. A same-registrable-domain rule alone breaks
sign-in, because `mail.google.com` to `accounts.google.com` is one session and
so is every OAuth hop and consent interstitial; and a link click, a scripted
navigation, a server redirect and a typed URL do not obviously deserve the same
answer. Getting that wrong is worse than the problem it solves, so this stage
gets its own design session before it gets requirements.

Open questions for that session, recorded so they are not rediscovered:
- What is "home" — a registrable domain, an origin, a URL prefix, or a set the
  user can edit?
- Do auth and consent hops get an allowance, and is it a list or a heuristic?
- Does the boundary apply to pinned entries too, or only favourites? Line 74
  currently grants pinned tabs free navigation with a revert affordance.
- Where does the new tab land — Today in the current space, or a temporary
  place that Stage 5's Little Arc might own?
- What happens to the favourite's own tab: does it stay where it was, or return
  home?

### Stage 3. Spaces and profiles

Goal: multiple spaces, each bound to a profile with its own sign-ins, in one window.

- R3.1 Create, rename, reorder, delete spaces; icon and gradient per space.
- R3.2 Switch space via bottom dots, horizontal swipe, and Ctrl+1..9 with a slide animation; content switches to the space's last active tab.
- R3.3 Each space has its own Favorites, Pinned and Today.
- R3.4 Create Arcium profiles; assign a profile to a space; tabs in that space use the profile's partition.
- R3.5 Log into the same site in two spaces with different profiles; sessions are independent.
- R3.6 New tabs, links and popups stay in their space and profile.
- R3.7 Per-profile clear data control.
- R3.8 Session restore restores every space and its active tab.

Acceptance:
- A3.1 Two spaces, two profiles, two accounts on the same site, both stay logged in across relaunch.
- A3.2 Space switch animation holds 60 fps in tracing.
- A3.3 Idle memory with three spaces and two profiles within NF2 plus the measured per-partition cost, which is recorded.

### Stage 4. Command bar and navigation

- R4.1 Cmd+T opens a centred command bar: URL, web search, open tabs across spaces, commands, history, bookmarks; Enter acts; Esc closes.
- R4.2 Cmd+L edits the URL of the current tab in the sidebar pill.
- R4.3 Little Arc: links from other apps open in a compact popup window with a single action to move the tab into a space.
- R4.4 Peek: a Favorite or Pinned link opens in a floating pane over the current page, dismissible, promotable to a tab.
- R4.5 Air Traffic Control: rules by domain pattern route URLs to a space and profile, applied to external links and new tabs.
- R4.6 Site search shortcuts configurable.

Acceptance: A4.1 open a link from Mail, it appears in Little Arc, routes to the right space via a rule. A4.2 command bar answers within one frame of typing in tracing.

### Stage 5. Layout

- R5.1 Split view with two to four panes, drag tabs into it, resizable dividers, reuse Chromium's native split view where it fits.
- R5.2 Sidebar auto-hide with hover reveal; Cmd+S toggles; resize by drag.
- R5.3 Compact mode with a floating URL bar.
- R5.4 Full-screen behaviour with sidebar hidden and reveal on hover.

Acceptance: A5.1 two-pane split with independent scrolling and navigation; A5.2 auto-hide reveal under 100 ms.

### Stage 6. Customisation and library

- R6.1 Boosts and Mods: per-site CSS and JS, element zaps, persisted per domain, toggleable.
- R6.2 Themes: gradient editor per space, light, dark and auto.
- R6.3 Media: auto picture-in-picture on tab switch; sidebar mini player with controls.
- R6.4 Library (WebUI, on demand): archived tabs, downloads, media, history; restore archived tabs.
- R6.5 Import bookmarks, passwords and history from Chrome, and spaces from Arc where its export allows.
- R6.6 Shortcut editor and Arcium settings section.

Acceptance: A6.1 a CSS boost persists across relaunch; A6.2 Library opens in under 300 ms and its process exits when closed.

### Stage 7. Performance and lifecycle

- R7.1 Sleeping tabs: inactive Today tabs unload after a configurable time and restore on click with their scroll position.
- R7.2 Inactive spaces discard their tabs by policy; favorites and pinned exempt by default.
- R7.3 Startup profile: no Arcium work before first paint beyond loading the live model.
- R7.4 Perf script gates: by this stage the budgets are calibrated and a regression fails the stage.

Acceptance: A7.1 50 tabs across 3 spaces, idle memory below vanilla Chrome with the same tabs after sleep kicks in.

### Stage 8. Distribution

- R8.1 Code signing and notarisation; R8.2 auto-update; R8.3 release build and packaging script; R8.4 documented monthly rebase routine with the perf gate.

Acceptance: A8.1 a signed build installs on a clean Mac and updates itself to the next build.

## 6. Risks and spikes

| Risk | Stage | Mitigation |
|---|---|---|
| Fixed-partition `SiteInstance` does not carry through all navigation paths (popups, prerender, back-forward cache) | 3 | Spike first: verify with a browser test before building the profile UI |
| Chrome Web Store rejects a non-Chrome user agent | 0 | Match Brave's approach; verify in A0.1 |
| Hiding the tab strip breaks Chromium code that assumes it exists (tab dragging, fullscreen, accessibility) | 1 | Keep the strip alive but zero-height first; remove later |
| Build times on a USB SSD | 0 | Component build, `use_remoteexec` off, ccache-style `cc_wrapper`, measure and record |
| Rebase conflicts grow with patch count | all | Logic in `arcium/`, patches are hooks only; rebase monthly, never skip |
| UI-thread jank from painting favicons and gradients | 1, 3 | Cache rendered gradients; measure NF4 from Stage 1 |

## 7. Deviations

Where a shipped stage differs from what this document says, the difference is recorded here rather
than by quietly editing the requirement, so a reader can tell an intentional change from a
mistake. A deviation is a place the spec was wrong or under-specified and the build corrected it.
Stages 0 and 1 shipped with none.

Distinct from Stages 2.5 and 2.6 in section 5: those are new work the spec never covered, not
departures from what it did say.

### Stage 2

**D2-1. Folders are Arcium entities, not Chromium tab groups.** Line 76 says a Folder is "built on
Chromium's tab group data where it fits". It does not fit, and `arcium/` contains no reference to
`tab_groups::` at all. Three reasons, in order of weight. A `TabGroup` belongs to one
`TabStripModel`, so it is per window; a folder must be per profile and appear identically in every
window, which is the same reason entries are not per window. A `TabGroup` can only contain live
tabs, but a folder's whole point is holding pinned entries that outlive their tabs and are usually
cold — a group cannot represent an entry with no tab. And a group is saved by Chromium's own
tab-group sync into storage Arcium does not own, which would put half the model in a file the
stage does not control and cannot migrate. `Folder` is therefore a plain struct in
`arcium/browser/model/folder.h`, serialized in the same JSON file as everything else, and Stage
2.5's nesting requirement is a change to that struct rather than a fight with an upstream type.

**D2-2. Today tabs have never-archive rules the spec does not mention.** R2.3 says Today tabs
auto-archive after the idle timeout, with no exceptions. Implemented exactly that way, the feature
eats the user's work, so `ArchiveService::MayArchive` refuses five cases: the window's **active
tab**; a tab **claimed by an entry** (a pinned or favourite tab is not a Today tab, whatever the
timeout says); a tab that is **currently audible**; a tab whose page has a **`beforeunload` or
`unload` handler**, because a page with something to say about being closed must not be closed
silently; and any tab belonging to **another window's strip**, which is another service's
business. A `kNever` timeout disables the sweep entirely, as R2.3 already allows.

The explicit "Clear" button is deliberately not bound by the first rule — it archives every
unclaimed tab including the active one, because that is a button the user pressed rather than an
automatic sweep. It still respects the claimed-by-an-entry rule, since clearing Today must not
delete pinned entries.

**D2-3. A custom title is permanent.** R2.4 says a renamed title "persists for pinned and
favorites", which reads as "survives a restart". It is stronger than that: `TabEntry` carries
`custom_title` and `last_title` as separate fields, `SyncEntryTitles()` only ever writes
`last_title`, and `DisplayTitle()` prefers `custom_title` whenever it is non-empty. So a rename is
never overwritten by the page's own title — not on navigation, not on a title change, not on
restore. The page title keeps updating underneath in `last_title` and reappears if the custom
title is set back to empty, which is the only way to undo a rename. The weaker reading, where the
page's title wins after a navigation, would make renaming a pinned tab useless precisely on the
pinned tabs that navigate.

**D2-4. A restored tab's idle clock comes from the tab, not from the model file.** Section 5 of
the Stage 2 spec says tabs restored after a restart "take the model's last-save time as their
last-active floor". They did, briefly, through a floor plumbed from `ModelStore` into
`ArchiveService`; it was deleted at `fc0e067` and replaced by `WebContents::GetLastActiveTime()`.
Session restore already carries the saved last-active time into `WebContents::CreateParams`, so
the tab itself knows when it was last on screen and that answer survives a quit — which is what
the floor was invented to supply. The floor was also worse than nothing: it was one clock for
every tab, so a tab genuinely used minutes before the quit was aged to the same instant as one
untouched for a week. The stated behaviour is unchanged — a browser closed overnight still
archives yesterday's Today tabs on launch — and there is now one source for a tab's idle time
instead of two, which is the failure mode this feature had already been bitten by once.

## 8. Testing strategy

- Unit tests for every model and service in `arcium/test/`, run with Chromium's test runner.
- Browser tests for tab-to-space mapping, partition isolation and persistence.
- Views playground for visual iteration; screenshots checked into `docs/screens/` per stage for comparison.
- Perf script results recorded per stage in `docs/perf/`.
- Manual acceptance lists above, executed and ticked before a stage is declared done.
