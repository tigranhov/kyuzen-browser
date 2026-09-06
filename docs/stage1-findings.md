# Stage 1 findings

Things learned while building the visual MVP that later stages need to know.
Same shape as `docs/stage0-carryover.md`.

| # | Finding | Where | Decision |
|---|---|---|---|
| 1 | GN forbids a dependency cycle even when `allow_circular_includes_from` is set; the pattern is one-directional deps plus includes without a dep | `arcium/ui/browser/BUILD.gn` | Adopted; documented in the BUILD file |
| 2 | grit assigns message id ranges by `.grd` path, so renamed string tables need an entry in `tools/gritsettings/resource_ids.spec` | patch 0015 | Registration-only patch |
| 3 | GN loads a `BUILD.gn` only if something references it; tests and the playground need a root reference | patch 0011, `arcium/BUILD.gn` | One group `//arcium:all` |
| 4 | `views::FlexSpecification(min, max)` applies to both axes; a spacer with `kUnbounded` inside a horizontal row makes the row claim all vertical space from its parent | `arcium/ui/sidebar/*.cc` | Always use the orientation-qualified constructor |
| 5 | One tab operation produces several `TabStripModelObserver` callbacks (insert, title, loading); observers must coalesce | `SidebarTabModel::NotifyChanged` | One posted task per burst, no timers |
| 6 | `BubbleDialogDelegateView` subclassing is closed to new code; use `BubbleDialogDelegate` plus a contents view, and the owner keeps both delegate and widget alive | `QuickEntryBubble` | Adopted |
| 7 | Screen capture from an automated session is blocked by macOS; offscreen paint of the Views tree works without permission | `view_snapshot.cc` | `--snapshot` / `--arcium-snapshot` switches |
| 8 | The dev profile keeps the profile name chosen at first run ("Your Chromium"); this is data, not a string | Local State | Fresh profiles read "Your Arcium" |

## Daily-driver checklist (Task 12)

Filled in as each item is exercised.

| Item | Result | Fix |
|---|---|---|
| New window | Opens with its own sidebar and model; closing it is clean | none needed |
| Incognito window | Sidebar present, dark palette from the colour mode | none needed |
| Popup window | No sidebar, Chromium layout untouched, no crash | none needed |
| Fullscreen | Entering fullscreen crashed: macOS immersive fullscreen moves top chrome into an overlay window, which is zero-sized when the tab strip and toolbar are hidden | patch 0100 turns immersive fullscreen off for Arcium windows; the sidebar keeps the full height and the page fills the rest |
| Bookmark bar | Shows above the page inside the reduced area, 34 px, to the right of the sidebar | none needed |
| Infobars | Show above the page at the top of the reduced area | none needed |
| Session restore | Six tabs restored as six rows in order | none needed |
| 60 tabs | Rows past the column height were laid out at zero height and vanished | Today list wrapped in a ScrollView |
| Downloads | The file downloads, but the progress ring and badge live in the hidden toolbar, so there is no visible indicator and the bubble has no anchor | Stage 6 gives downloads a home in the sidebar |
| Extension actions | Not exercised: extension icons also live in the hidden toolbar | Stage 6, together with downloads |
| Find in page | Not exercised: needs a real key press, which this environment cannot send | run by hand during the acceptance day |

## Verification method

Every row above was exercised by driving the browser over the DevTools
protocol and reading the window's view tree from the `--arcium-snapshot`
log, which prints class, bounds and visibility for every view. Screen
capture needs a macOS permission the agent does not have.
