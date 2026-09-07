# Patches

Ordered series applied to the Chromium checkout by `scripts/sync`.

Naming: `NNNN-short-name.patch`, four-digit, gaps of 10 to allow insertion.

Every patch header states: what upstream seam it hooks, why the hook is needed, and which
`arcium/` code it delegates to. A patch contains no logic, only the hook.

Two patches must never write into one hunk's context. `scripts/sync` decides a patch is already
applied by `git apply --reverse --check`, so a second patch that adds lines inside the first's
context makes that probe fail and `sync` reports a CONFLICT — on the *second* run, never the
first. Give the second hook a seam of its own, or fold it into the patch that already owns those
lines.

## Status

Stage 0 shipped with zero patches: branding is selected by GN (`branding_path_component`) and
Google services are removed by GN args in `build/common.gni`.

Stage 1 adds twelve. Nine are hooks of a few lines each that call into `arcium/`. Three carry no
call and say so in their header, because Chromium keys them by file name and there is nowhere to
delegate to: `0010` and `0011` are GN wiring, and `0015` registers message id ranges for the
renamed string tables. The inventory with the seam and reason for each lives in
`docs/superpowers/plans/2026-09-06-stage-1-visual-mvp.md`.

Stage 2 adds five. Three are hooks that carry the entry id across the places Chromium drops it
(`0120`, `0130`, `0140`); the other two carry no call and say so in their headers — `0110` is a
registration table Chromium keys by name, `0125` is GN wiring.

| Patch | Seam | Delegates to |
|---|---|---|
| `0110-sql-archive-tag.patch` | `tools/metrics/histograms/metadata/sql/histograms.xml`, `DatabaseTag` variants | nothing — a registration table Chromium keys by name |
| `0120-restore-tab-entry.patch` | `chrome::CreateRestoredTab`, `chrome::AddRestoredTabImpl` and `chrome::ReplaceRestoredTab` in `chrome/browser/ui/browser_tabrestore.cc` | `arcium::StashRestoredEntryId`, `arcium::BindStashedEntryId` |
| `0125-gn-sessions-arcium.patch` | `chrome/browser/sessions/BUILD.gn` `source_set("impl")` — that target only | nothing — GN wiring for 0130 |
| `0130-session-tab-commands.patch` | `SessionService::BuildCommandsForTab` in `chrome/browser/sessions/session_service.cc` | `arcium::AppendTabEntryCommand` |
| `0140-live-tab-extra-data.patch` | `BrowserLiveTabContext::GetExtraDataForTab` in `chrome/browser/ui/browser_live_tab_context.cc` | `arcium::PopulateTabEntryExtraData` |

`0125` covers `//chrome/browser/sessions:impl` and nothing else. `0120` and `0140` need no GN
wiring: both files build in `//chrome/browser/ui:ui`, which reaches `//arcium/browser` through
patch `0010`'s dep on `//arcium/ui/browser` and that target's `public_deps`. Adding a second
`//arcium/browser` line to `chrome/browser/ui/BUILD.gn` was tried and reverted: it lands inside
patch `0010`'s hunk context, which breaks `0010`'s already-applied check and makes `scripts/sync`
non-idempotent — the rule at the top of this file, met in practice. `0125`'s own header carries
the long form.

## Monthly rebase routine

1. Find the new stable tag on https://chromiumdash.appspot.com/releases?platform=Mac
2. `scripts/rebase <tag>`; fix any conflicting patch by moving its hook to a more stable seam.
3. `scripts/build dev`, run the last completed stage's acceptance list by hand.
4. `scripts/netaudit 60`, then `scripts/perf --label rebase-<tag>` when the machine is quiet.
5. Commit `CHROMIUM_VERSION` and any patch changes together with a message naming the tag.
