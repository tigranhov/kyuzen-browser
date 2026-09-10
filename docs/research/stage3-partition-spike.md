# Stage 3 spike: does a fixed storage partition survive every way a tab is made?

Run 2026-09-10 against Chromium 152.0.7977.83, dev build (`dcheck_always_on =
true`). Throwaway: the code that ran it was reverted and is not in any commit.
This document is what it found.

## Question

Spec §4.4 builds Arcium profiles on `content::StoragePartitionConfig` inside
the one Chromium profile, with each tab on a `SiteInstance` fixed to its
profile's partition. The risk table (§6) asks for this to be proved before any
profile UI is built, because a partition that leaks on one navigation path
fails R3.5 silently: two spaces quietly share a login and nothing looks wrong.

If fixed partitions could not be held, the fallback is real Chromium profiles,
which means several `Browser` objects and breaks "one window, one `Browser`".
So the answer decides the Stage 3a data model, not only Stage 3b.

## Method

A temporary edit to `CreateTargetContents`
(`chrome/browser/ui/navigator/browser_navigator.cc:466`): under
`--arcium-spike-partition`, a new tab on a `p.*` host got
`SiteInstance::CreateForFixedStoragePartition` with the partition
`arcium-spike/p`. Every other tab took Chromium's normal path, so what a child
tab inherited was Chromium's own behaviour, not the edit's.

A local server answered as `p.localhost` and `q.localhost`. Each host carried a
cookie `who=default` set in the default partition and `who=spike` set in the
spike partition; each probe opened `q.localhost/show.html`, which reads it back.
`spike` means the tab stayed in the partition, `default` means it leaked, and
`none` means neither. A second cookie marked `SameSite=None; Secure` served the
cross-site iframe, which never receives the default `Lax` one.

A Node script drove the browser over the DevTools protocol with real input
events, so Cmd+click and user-gesture checks behaved as a person's click. Every
run used a fresh throwaway user-data directory. A control run without the flag
checked the harness itself; a calibration step loaded the same page in a
default-partition tab and read `who=default iframe=default`, as it must.

## Results

| Path | Result | Created by |
|---|---|---|
| Typed URL, same tab, cross-site | **holds** | navigation in the existing `WebContents` |
| Link click, same tab, cross-site | **holds** | same |
| Back, restored from the back/forward cache | **holds** (`persisted = true`) | same |
| Cross-site iframe | **holds** | same `BrowsingInstance` |
| `window.open` with an opener | **holds** | content, `CreateNewWindow`, opener's `SiteInstance` |
| `target=_blank rel=opener` | **holds** | same |
| `target=_blank` (implicitly `noopener`) | **leaks** | content, `CreateNewWindow`, `SiteInstance::Create` |
| `window.open(..., "noopener")` | **leaks** | same |
| Cmd+click (background tab) | **leaks** | chrome, `CreateTargetContents`, `GetSiteInstanceForNewTab` |
| Relaunch with `--restore-last-session` | **leaks**: every restored tab, the spike tab included, came back in the default partition | chrome, `CreateRestoredTab`, `GetSiteInstanceForNewTab` |
| Stage 2.6 home-boundary divert | **leaks** (by code, not run) | `OpenURL` → `Navigate` → `CreateTargetContents` with no opener, the Cmd+click path |
| Speculation-rules prerender | **leaks or is cancelled** (by code, not run) | `SiteInstanceImpl::Create(browser_context)` |

The prerender row is read from the code, not observed: the harness never got a
prerender to activate, in the control run either, so the test proved nothing in
either direction. `PrerenderHost` builds the prerender frame tree from
`SiteInstanceImpl::Create` or `CreateForURL`
(`content/browser/preloading/prerender/prerender_host.cc:525`), both of which
know only the browser context, so a prerendered page would be built in the
default partition.

**The pattern is exact: a new tab keeps its partition if and only if it has an
opener.** Everything that makes a tab without one — `noopener`, a modified
click, `OpenURL`, session restore — asks for a `SiteInstance` knowing only the
profile, and gets the default partition.

## Where the hooks go

Three seams cover every leak observed, and one of them is already patched.

1. **`CreateTargetContents`** (chrome). Cmd+click, "Open link in new tab",
   the home-boundary divert, and any new tab Arcium itself opens. `NavigateParams`
   carries `source_contents`, so the hook can give the new tab the source tab's
   partition, or the active space's profile when there is no source.
2. **`CreateRestoredTab`** (chrome, `browser_tabrestore.cc:77`). Already hooked
   by patch 0120 for the entry id. Chromium's session does not record a tab's
   partition, so the profile id must ride in `extra_data` beside the entry id
   and choose the `SiteInstance` here.
3. **`WebContentsImpl::CreateNewWindow`**, the `opener_suppressed` branch
   (content, `web_contents_impl.cc:5557`). This is the awkward one. `content`
   cannot depend on `arcium/`, so a hook there cannot call into Arcium the way
   the other patches do. The branch already preserves the partition for
   `<webview>` guests; the smallest change widens that to any non-default
   partition, which reads more like an upstream bug fix than Arcium logic. The
   alternative is a new `ContentBrowserClient` method that Chrome's client
   answers by calling into `arcium/`. Stage 3b's design decides.

Prerender needs no seam of its own if Stage 3b turns preloading off for tabs in
a non-default partition, which R3.9's rule — no page loads unless the user asks
— argues for anyway.

## Open

- **One renderer DCHECK, not reproduced.** The first partitioned run hit
  `DCHECK failed: !associated_receiver_.is_bound()` in
  `blink::DevToolsAgent::BindReceiver`, in a renderer, and that renderer died.
  It did not recur in the control run or the second partitioned run. It fires
  while DevTools binds to a frame, so it may be the driver's doing, but one run
  in two with a partition against none without is not enough to say so. Stage
  3b's browser tests, which do not attach DevTools, will show whether it is
  real.
- **Paths not exercised:** a new tab from Cmd+T or the sidebar (Arcium's own
  code decides that one), drag of a link onto the sidebar, Shift+click (a new
  window, which Arcium's one-window model has to refuse or redirect anyway),
  downloads, and service workers.

## Recommendation

Fixed partitions hold for every path that stays inside a `BrowsingInstance`,
and every path that leaks goes through one of three identifiable seams. Spec
§4.4's design stands; there is no case for falling back to Chromium profiles.
§4.4's sentence "Popups and child frames inherit the partition" is true only
for popups with an opener and should be corrected when Stage 3b is written.
