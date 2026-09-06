# Stage 1: Visual MVP Design

Date: 2026-09-06
Status: Approved 2026-09-06
Parent: `2026-09-05-arcium-browser-design.md`, section 5 Stage 1 (R1.1 to R1.9) and section 4.6
Inputs: `docs/stage0-carryover.md`; mockups in `.superpowers/brainstorm/3531-1788668664/content/`

## 1. Goal

The complete sidebar-first layout on screen, bound to live tabs, usable as a daily browser, so look
and feel can be judged before any deep feature is built. One space, one profile, no persistence of
Arcium state beyond Chromium's own session restore.

## 2. Visual decisions (from the mockup review)

| Decision | Choice |
|---|---|
| Structure | Zen classic: navigation rows at the top, favourites grid, pinned list, divider, today list, new tab row, bottom bar |
| Top rows | Row 1: traffic lights, sidebar-toggle button; back, forward, reload right-aligned. Row 2: URL pill, full width, editable in place |
| Colour | Subtle tint: two-stop gradient of the space colour from the top corner into the neutral surface. Light and dark follow the system |
| Density | Comfortable: 32 px rows, 16 px favicons, 4 favourites tiles per row, 250 px sidebar width (fixed this stage) |
| Bottom bar | Space chips, active one expanded with its name; profile badge on the right. Right-click menu present with items disabled until Stages 3 and 6 |
| Divider | Reveals "Clear" on hover; closes every Today tab |
| Cmd+T | Floating quick-entry bubble centred over the page: URL or search, Enter opens a new Today tab, Esc dismisses. Plain text field; suggestions arrive with the command bar in Stage 4 |
| Cmd+L | Focuses the URL pill for in-place editing |
| Content area | Inset on the tinted background with rounded corners; native tab strip and toolbar hidden |

## 3. Components

All in `arcium/ui/sidebar/`, Views, one class per file pair, each hostable in the Views playground
against a fake model.

| Class | Responsibility |
|---|---|
| `SidebarView` | Column container. Owns the rows below, applies the tint background, forwards the tab model to sections |
| `NavRowView` | Sidebar-toggle button and the back, forward, reload buttons bound to Chromium's existing commands |
| `UrlPillView` | Slot for the location bar (see 4.2). Fallback: displays the formatted URL and opens the quick entry on click |
| `FavoritesGridView` | Grid of favourite tiles, 4 per row. In-session set for this stage |
| `PinnedListView` | Rows for Chromium's pinned tabs |
| `SectionDividerView` | Hairline with the hover-revealed Clear action |
| `TodayListView` | Rows for every tab that is neither favourite nor pinned, plus the New tab row. Drag reorder |
| `TabRowView` | Favicon, title, throbber, alert indicator, hover close button. One row per tab, 32 px |
| `SpaceBarView` | Space chips and profile badge; context menu scaffold |
| `QuickEntryBubble` | Cmd+T floating entry |
| `SidebarTabModel` | Non-UI: observes the window's tab model, derives per-tab section and row state, notifies the views. Unit-tested without a browser |

## 4. Integration with Chromium

Every touch point is a hook patch that delegates into `arcium/`, per CLAUDE.md.

### 4.1 Window composition

The browser window view keeps its tab strip and toolbar alive but collapsed to zero height, because
fullscreen, tab dragging and accessibility code assume they exist. The sidebar is inserted to the left
of the contents container. The contents container is inset and rounded through the same mechanism
Chromium's split view uses for its panes.

### 4.2 URL pill

Preferred: the toolbar's real location bar is reparented into `UrlPillView`, inheriting the omnibox,
suggestions, security indicators and page-action icons. This is the stage's one risky move and gets a
spike before the plan commits to it. Fallback: a custom pill that renders the formatted URL and hands
editing to the quick entry.

### 4.3 Navigation

Back, forward and reload reuse Chromium's toolbar buttons bound to the existing browser commands.

### 4.3a Chromium's native vertical tab strip: not used

Chromium 152 ships a vertical tab strip behind a flag (`chrome/browser/ui/views/frame/vertical_tab_strip_region_view.*`).
Decision (2026-09-06): Arcium builds its own sidebar. Chromium's strip keeps the address bar in a top
toolbar, has no favourites, today or spaces sections, and is new code that will change every release,
so hooks into it would conflict on every rebase. It is used only as a reference for how a left panel
coexists with the window's caption buttons.

Building blocks reused from Chromium where they are libraries, not layouts: `TabRendererData` (title,
favicon, loading and alert state per tab), `TabIcon` (favicon with throbber), `AlertIndicatorButton`,
`ToolbarButton` for navigation, `NewTabButton`, `views::ResizeArea` (Stage 5), the colour provider
and `ui::ColorMixer` for theming, and `views::BubbleDialogDelegateView` for the quick entry.

### 4.4 Data flow

`SidebarTabModel` implements Chromium's tab model observer, the same interface the native tab strip
uses. Section mapping: Chromium's pinned state is the Pinned section; a small in-session set is
Favorites; everything else is Today. Favicons, titles, loading and audio state come from the per-tab
renderer data Chromium already maintains. Drag reorder within Today calls the model's move operation.

### 4.5 Theme

Sidebar and window colours are added to Chromium's colour provider through an Arcium colour mixer.
The tint is a two-stop gradient rendered once per size change and cached as a shader.

### 4.6 Stage 0 carry-over included

- Arcium product strings via `branding_path_product = "arcium"` and symlinked string tables
- Hook to suppress the "Google API keys are missing" infobar
- Sign-in controls in Settings hidden by a pref default
- Chrome brand presented in user-agent client hints so the Web Store treats Arcium as Chrome

## 5. Performance

- No new processes. Everything runs in the browser process on the UI thread.
- No per-tab work beyond one row view and one observer callback per model change.
- The gradient is painted from a cached shader; hover and selection repaint only the affected row.
- Startup: the sidebar is built with the window, from data Chromium already has. No I/O.

## 6. Testing

- Unit tests for `SidebarTabModel` section mapping and row state derivation.
- A browser test that opens, pins, closes and reorders tabs and asserts the sidebar sections match the model.
- Playground screenshots of every component in `docs/screens/stage1/`.
- Acceptance A1.1 to A1.3 from the master spec, plus `scripts/netaudit` and `scripts/perf --label stage1`.

## 7. Risks

| Risk | Mitigation |
|---|---|
| Location bar cannot be reparented cleanly | Spike first; fallback pill defined in 4.2 |
| Chromium code assumes a visible toolbar or tab strip | Collapse, never remove; browser test covers fullscreen and new-window |
| First patches to the tree | Hooks only, one seam each, header explains why |
| Product strings: other languages still say Chromium | Accepted this stage; English is replaced |
