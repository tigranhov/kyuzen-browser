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
| New window (Cmd+N) | pending | |
| Incognito window | pending | |
| Popups and app windows | pending | |
| Fullscreen and video fullscreen | pending | |
| Bookmark bar and infobars | pending | |
| Find in page | pending | |
| Downloads bubble | pending | |
| Extension actions | pending | |
| Session restore | pending | |
| 60 tabs | pending | |
