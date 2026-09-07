# Patches

Ordered series applied to the Chromium checkout by `scripts/sync`.

Naming: `NNNN-short-name.patch`, four-digit, gaps of 10 to allow insertion.

Every patch header states: what upstream seam it hooks, why the hook is needed, and which
`arcium/` code it delegates to. A patch contains no logic, only the hook.

## Status

Stage 0 shipped with zero patches: branding is selected by GN (`branding_path_component`) and
Google services are removed by GN args in `build/common.gni`.

Stage 1 adds twelve. Nine are hooks of a few lines each that call into `arcium/`. Three carry no
call and say so in their header, because Chromium keys them by file name and there is nowhere to
delegate to: `0010` and `0011` are GN wiring, and `0015` registers message id ranges for the
renamed string tables. The inventory with the seam and reason for each lives in
`docs/superpowers/plans/2026-09-06-stage-1-visual-mvp.md`.

Stage 2 adds four. Two are hooks for session restore (`0120`, `0130`); the other two carry no call
and say so in their headers — `0110` is a registration table Chromium keys by name, `0125` is GN
wiring.

| Patch | Seam | Delegates to |
|---|---|---|
| `0110-sql-archive-tag.patch` | `tools/metrics/histograms/metadata/sql/histograms.xml`, `DatabaseTag` variants | nothing — a registration table Chromium keys by name |
| `0120-restore-tab-entry.patch` | `chrome::CreateRestoredTab` and `chrome::AddRestoredTabImpl` in `chrome/browser/ui/browser_tabrestore.cc` | `arcium::StashRestoredEntryId`, `arcium::BindStashedEntryId` |
| `0125-gn-sessions-arcium.patch` | `chrome/browser/sessions/BUILD.gn` `source_set("impl")`, `chrome/browser/ui/BUILD.gn` `static_library("ui")` | nothing — GN wiring for 0120 and 0130 |
| `0130-session-tab-commands.patch` | `SessionService::BuildCommandsForTab` in `chrome/browser/sessions/session_service.cc` | `arcium::AppendTabEntryCommand` |

## Monthly rebase routine

1. Find the new stable tag on https://chromiumdash.appspot.com/releases?platform=Mac
2. `scripts/rebase <tag>`; fix any conflicting patch by moving its hook to a more stable seam.
3. `scripts/build dev`, run the last completed stage's acceptance list by hand.
4. `scripts/netaudit 60`, then `scripts/perf --label rebase-<tag>` when the machine is quiet.
5. Commit `CHROMIUM_VERSION` and any patch changes together with a message naming the tag.
