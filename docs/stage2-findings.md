# Stage 2 findings

Things learned while building the Arc tab model that later stages need to know.
Same shape as `docs/stage1-findings.md`.

| # | Finding | Where | Decision |
|---|---|---|---|
| 1 | `SessionID` is not stable across a restart: `SessionTabHelper` assigns `SessionID::NewUnique()` in its constructor unconditionally, and `SessionIdGenerator::SetHighestRestoredID` — the API that would make restored ids survive — has no production caller | patch 0120 | The entry id rides in Chromium's per-tab session `extra_data` instead, which does survive |
| 2 | The restore seam is two seams. In `CreateRestoredTab` the `WebContents` is a `unique_ptr` not yet in any tab strip, so there is no `tabs::TabInterface` and no `tabs::TabHandle`; the two upstream features that read `extra_data` there work only because they store their state on the `WebContents` | patch 0120, `arcium/browser/session_tab_entry.cc` | Stash on the `WebContents` in `CreateRestoredTab`, bind in `AddRestoredTabImpl` after the existing `from_session_restore` block |
| 3 | `SessionService::AddTabExtraData` is a silent no-op during a command rebuild: it early-returns unless the window is in `windows_tracking_`, and `SessionServiceBase::BuildCommandsForBrowser` inserts the window only *after* its loop over `BuildCommandsForTab` | patch 0130 | The hook passes `command_storage_manager()` and appends a rebuild command, like every other per-tab command there |
| 4 | A rebuild is the only thing that writes the key, and an automatic one comes every `kWritesPerReset` = 250 commands — so a short session would never write it | `arcium/ui/browser/session_rebuild_nudge.cc` | A posted, coalesced `ResetFromCurrentBrowsers()` after any binding change. One writer plus a nudge, never a second write path |
| 5 | `session_service.cc` builds in `//chrome/browser/sessions:impl`, not `:sessions`. `//chrome/browser/ui:ui` needed no patch at all: patch 0010 already gives it `//arcium/ui/browser`, and promoting that target's `//arcium/browser` dep to `public_deps` — correct anyway, since `sidebar_tab_model.h` includes `tab_binding.h` — makes it transit | patch 0125, `arcium/ui/browser/BUILD.gn` | 0125 wires `:impl` **only**. Adding a second `//arcium/browser` line to `chrome/browser/ui/BUILD.gn` was tried first and reverted: those lines land inside patch 0010's hunk context, so 0010's `git apply --reverse --check` already-applied probe stops matching and `scripts/sync` reports `CONFLICT 0010-gn-arcium-ui.patch` — on the **second** run, never the first. Two patches must not write into one hunk. `gn check` on `//chrome/browser/sessions:impl` and `//chrome/browser/ui:ui` is what says whether either is still right after a rebase |
| 6 | `//chrome/browser/sessions` does not reach `//chrome/browser/ui:ui`, so `//arcium/ui/browser` can depend on it without joining patch 0010's cycle | `arcium/ui/browser/BUILD.gn` | `SessionServiceFactory` is reachable from the Arcium UI side |
| 7 | A tab closed *while the browser runs* loses the key. `BrowserLiveTabContext::GetExtraDataForTab` builds the closed tab's `extra_data` from scratch and populates glic only — the session file's copy is not consulted — so reopening the tab gave an unclaimed Today row beside the entry's own cold row: two sidebar rows for one page. A tab closed in a *previous* session is fine: `TabRestoreServiceImpl` carries `extra_data` out of the session file into `chrome::AddRestoredTab`, which patch 0120 already hooks | patch 0140 | A third writer at that seam, sharing `IsClaimedByEntry` with the rebuild writer so a stale binding contributes nothing on either path |
| 8 | `Cmd+Shift+T` does not take `ReplaceRestoredTab`. It is `RestoreEntryById(..., WindowOpenDisposition::UNKNOWN)`, and `TabRestoreServiceHelper::RestoreTab` routes to `ReplaceRestoredTab` only for `CURRENT_TAB`; the ordinary gesture goes to `AddRestoredTab` → `AddRestoredTabImpl` | patch 0120 | `ReplaceRestoredTab` is hooked anyway — same stash, same lifetime, and leaving it out would park an id on a `WebContents` that nothing reads — but it is not what makes the reopen gesture work |
| 9 | `tabs::TabInterface::GetFromContents` dereferences its lookup unconditionally (`components/tabs/impl/tab_interface.cc`), so an `if (!tab)` guard after it is dead code that reads as protection. `MaybeGetFromContents` is the null-safe one | `arcium/browser/session_tab_entry.cc` | Both lookups use `MaybeGetFromContents`, so the guards are real. At a patched seam the failure mode has to be a cold entry, not a crashed browser |
| 10 | `content::BrowserContext` exposes no route from an off-the-record context to its parent — only `IsOffTheRecord()`. `arcium/browser` therefore *cannot* write regular-profile state from an incognito `WebContents`, whatever it does | `arcium/browser/session_tab_entry.cc` | The incognito session path is safe by construction rather than by a check. Tested anyway (`AnIncognitoTabNeitherBindsNorWritesRegularState`), because the argument is only as good as the API staying that way |

## Session restore acceptance list — NOT YET RUN

These need a human at the window and a `chrome` build, neither of which Task 12
or its fix round did. Nothing below has been executed. Run them before Stage 2
is called done. Scenarios 1-3 span two browser lifetimes; scenario 4 is one.

```bash
scripts/build dev chrome && scripts/run
```

| # | Scenario | Expected |
|---|---|---|
| 1 | Pin two tabs, leave them open, quit with Cmd+Q, relaunch with `scripts/run --restore-last-session` | Both pinned rows come back **warm** — bound to the restored tabs, not opening a second copy of the page when clicked |
| 2 | Pin a tab, open and close twenty tabs to provoke a command reset, quit, relaunch | The pinned row is still warm. This is the path patch 0130 exists for |
| 3 | Pin a tab, quit, delete `"$HOME/Library/Application Support/Arcium-dev/Default/Sessions"`, relaunch | The entry comes back **cold**, not missing. A lost key degrades to a cold entry, which is why the feature is safe to depend on |
| 4 | Pin a tab, close it with Cmd+W, reopen it with Cmd+Shift+T — one lifetime, no quit | Exactly **one** row for that page: the pinned entry goes warm again. Two rows means patch 0140's contribution is not reaching `GetExtraDataForTab`. This is the only scenario here that needs a single lifetime |

Worth watching while running these: whether scenario 1 needs the nudge at all
(the quit path may already rebuild), and how many `ResetFromCurrentBrowsers()`
calls a normal pinning session provokes. The nudge is coalesced to one posted
rebuild per burst, but no automated test covers it — `SessionServiceFactory`
has no service under `BrowserWithTestWindowTest`.
