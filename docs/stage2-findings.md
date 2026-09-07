# Stage 2 findings

Things learned while building the Arc tab model that later stages need to know.
Same shape as `docs/stage1-findings.md`.

| # | Finding | Where | Decision |
|---|---|---|---|
| 1 | `SessionID` is not stable across a restart: `SessionTabHelper` assigns `SessionID::NewUnique()` in its constructor unconditionally, and `SessionIdGenerator::SetHighestRestoredID` — the API that would make restored ids survive — has no production caller | patch 0120 | The entry id rides in Chromium's per-tab session `extra_data` instead, which does survive |
| 2 | The restore seam is two seams. In `CreateRestoredTab` the `WebContents` is a `unique_ptr` not yet in any tab strip, so there is no `tabs::TabInterface` and no `tabs::TabHandle`; the two upstream features that read `extra_data` there work only because they store their state on the `WebContents` | patch 0120, `arcium/browser/session_tab_entry.cc` | Stash on the `WebContents` in `CreateRestoredTab`, bind in `AddRestoredTabImpl` after the existing `from_session_restore` block |
| 3 | `SessionService::AddTabExtraData` is a silent no-op during a command rebuild: it early-returns unless the window is in `windows_tracking_`, and `SessionServiceBase::BuildCommandsForBrowser` inserts the window only *after* its loop over `BuildCommandsForTab` | patch 0130 | The hook passes `command_storage_manager()` and appends a rebuild command, like every other per-tab command there |
| 4 | A rebuild is the only thing that writes the key, and an automatic one comes every `kWritesPerReset` = 250 commands — so a short session would never write it | `arcium/ui/browser/session_rebuild_nudge.cc` | A posted, coalesced `ResetFromCurrentBrowsers()` after any binding change. One writer plus a nudge, never a second write path |
| 5 | `session_service.cc` builds in `//chrome/browser/sessions:impl`, not `:sessions`; and `//chrome/browser/ui:ui` cannot see `//arcium/browser` headers, because patch 0010's `//arcium/ui/browser` lists that under `deps` rather than `public_deps` and so does not transit | patch 0125 | GN-wiring patch adds `//arcium/browser` to both targets. `gn check` on both is what says whether it is still needed after a rebase |
| 6 | `//chrome/browser/sessions` does not reach `//chrome/browser/ui:ui`, so `//arcium/ui/browser` can depend on it without joining patch 0010's cycle | `arcium/ui/browser/BUILD.gn` | `SessionServiceFactory` is reachable from the Arcium UI side |

## Session restore acceptance list — NOT YET RUN

These three need a human at the window across two browser lifetimes and a
`chrome` build, neither of which Task 12 did. Nothing below has been executed.
Run them before Stage 2 is called done.

```bash
scripts/build dev chrome && scripts/run
```

| # | Scenario | Expected |
|---|---|---|
| 1 | Pin two tabs, leave them open, quit with Cmd+Q, relaunch with `scripts/run --restore-last-session` | Both pinned rows come back **warm** — bound to the restored tabs, not opening a second copy of the page when clicked |
| 2 | Pin a tab, open and close twenty tabs to provoke a command reset, quit, relaunch | The pinned row is still warm. This is the path patch 0130 exists for |
| 3 | Pin a tab, quit, delete `"$HOME/Library/Application Support/Arcium-dev/Default/Sessions"`, relaunch | The entry comes back **cold**, not missing. A lost key degrades to a cold entry, which is why the feature is safe to depend on |

Worth watching while running these: whether scenario 1 needs the nudge at all
(the quit path may already rebuild), and how many `ResetFromCurrentBrowsers()`
calls a normal pinning session provokes. The nudge is coalesced to one posted
rebuild per burst, but no automated test covers it — `SessionServiceFactory`
has no service under `BrowserWithTestWindowTest`.
