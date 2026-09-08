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

| 11 | Under load, `arcium_unittests` reported six failures on one run and passed on every other. Two of the six were `SidebarViewsTest.PressingTheButtonWhileTheListIsClosingOpensNothing` and `TabSearchServiceTest.SuppressedNewArchiveRowsDoNotHideOlderMatches`, both timing-sensitive around `Browser`'s 200 ms coalescing window | `arcium/test/` | Recorded, not chased. Eight full runs since (three in Task 13A, five in 13B at load average 96-111) and 30 back-to-back iterations of the two named tests were all green, so it is not reproducible on demand. If it recurs on a **quiet** machine it is a real flake in those tests and worth a fix; under load it is the machine. See "The flaky run" below |

## Acceptance A2.1 and A2.2 — EXECUTED 2026-09-08, PASSED

Run by hand on the machine, against `aaca21e`. Every item passes. Four defects
were found and fixed during the pass; each is listed below with its commit,
and every one of them was invisible to a suite that was green throughout.

**What the pass found, and why the tests missed it.**

| # | Defect | Why no test caught it | Fix |
|---|---|---|---|
| A | Dragging any row aborted the browser: neither drag source set a drag image, and `DragDropClientMac::StartDragAndDrop` DCHECKs a zero-size one | `View::DoDrag` enters a nested platform loop no headless harness drives, so 29 drag tests covered every drop *semantic* and never performed a drag | `630e38e` |
| B | The first two-finger scroll over the sidebar aborted the browser: both scroll views were built `ScrollWithLayers::kDisabled`, and on macOS the compositor always owns a scroll input handler, which `ScrollView::OnScrollEvent` DCHECKs against | The DCHECK needs a compositor with a scroll input handler, which the test environment has no reason to build. The test now asserts the state the DCHECK reads | `04c5836` |
| C | The close button flickered under the cursor, and the row's hover background blinked with it: the button appeared under the pointer, which made the row "not hovered" by Views' default rule, which hid the button, which re-hovered the row | The fixture's `Hover()` helper calls `OnMouseEntered` directly, so the event processor never held the row as its target and never sent the exit. The first version of the new test passed against the bug | `cbd206c` |
| D | Pinned sat outside the scroll viewport and kept its full height regardless, so enough pins pushed Today off the bottom of the panel entirely | No test asserted where the sections live relative to the viewport; every test that touched a list built the list directly | `aaca21e` |

Defect C is the third time this stage a test agreed with broken code, after the
two the final review found. The pattern is the same each time: a helper or a
fixture substitutes for the real path, and the substitution is exactly where the
bug lives. Worth remembering as the stage's most durable lesson.

The original table follows, in the shape Stage 1's daily-driver checklist used. It is written in the shape Stage 1's
daily-driver checklist used, ready to be filled in by the human pass, and Stage 2
is not done until it is. A2.1 in the spec is one line — "Pin, favorite, rename,
fold; quit and relaunch; everything is where it was" — and the rows below are
that line decomposed into things a person can actually observe, one per model
command the stage shipped.

**Where the results come from.** This pass is tracked on the human test checklist
at https://claude.ai/code/artifact/dd8e000a-b469-4dd9-878a-e619bb1705b4, which
also carries acceptance A2.2 (the archive pass, which uses the
`--arcium-fake-clock-offset` switch Task 13A added) and the session-restore
scenarios in the next section. Copy results back into these tables when the pass
is run; a row that had to be fixed records the fix, as Stage 1's did.

```bash
scripts/build dev chrome && scripts/run
```

| Item | Result | Fix |
|---|---|---|
| Drag a Today tab above the divider pins it (`PinTab`) | not run | |
| Drag a tab into the Favorites grid makes a Favorite (`AddToFavorites`) | not run | |
| A Favorite with no live tab opens its `home_url`; with one, focuses it (`ActivateEntry`) | not run | |
| A pinned tab navigated away offers "return to pinned URL" on hover and by shortcut (`ReturnToPinnedUrl`) | not run | |
| Rename a row inline; the custom title sticks and is not overwritten by the page's own title (`SetEntryTitle`) | not run | |
| Create a folder from a pinned entry (`CreateFolderWithEntry`), rename it (`SetFolderName`), collapse it (`SetFolderCollapsed`) | not run | |
| Drag a pinned entry into and out of a folder (`MoveEntryToFolder`) | not run | |
| Reorder within a section, and move an entry between sections, by drag (`MoveEntryToSection`) | not run | |
| Unpin an entry; its live tab survives as a Today row rather than closing (`UnpinEntry`) | not run | |
| Every row type's context menu offers its commands and nothing that does not apply | not run | |
| **Quit with Cmd+Q and relaunch.** Pinned entries, favourites, folders, custom titles, folder names, collapsed state and order are all where they were | not run | |
| A second window on the same profile shows the same entries, and a change in one window appears in the other | not run | |

The last two rows are the ones that matter most: the first is A2.1's actual
sentence, and the second is the claim in the perf record that `ModelStore` is
owned per profile and shared by every window, which nothing else on this list
exercises.

## The flaky run

Recorded so it is a note rather than folklore. During Task 13A the suite run
immediately after a six-minute build reported 6 failures; five subsequent runs
were clean. Task 13B tried to reproduce it and could not:

| Attempt | Result |
|---|---|
| 5 consecutive full runs, load average 96-111 | 286/286 each time |
| `--gtest_repeat=30` over the two named tests | 60/60 passed |

Suite wall time across those runs varied from 28 s to 36 s, and in 13A from 31 s
to 76 s, which is the size of the machine's own variance. The one thing not
reproduced is 13A's exact condition — a run starting the instant a large build
released ten cores and a USB SSD. That is the most likely explanation and it is
not a defect in the code. Treat a repeat on a **quiet** machine as a real bug in
those two tests; treat one under load as the machine, and say which it was.

## Session restore acceptance list — EXECUTED 2026-09-08, PASSED

Run by hand. All four scenarios behave as specified: entries come back warm
where a session restored their tabs, cold where the Sessions directory was
deleted, and Cmd+Shift+T produces exactly one row.

A note on what this feature is, because the acceptance wording misled its own
reader during the pass: **pins and favourites never depend on session restore.**
They live in Arcium's JSON model store and come back whatever Chromium's session
does — deleting the Sessions directory yields a cold entry, never a missing one.
Session restore decides only whether a restored entry is warm (bound to a live
tab, so clicking focuses it) or cold (clicking opens the URL). The only failure
it prevents is a duplicate tab. Scenarios 1-3 span two browser lifetimes; scenario 4 is one.

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
