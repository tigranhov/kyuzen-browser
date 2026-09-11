# Stage 3b research: how Arcium profiles map onto storage partitions

Checkout: /Volumes/Texternal/chromium/src (Chromium 152.0.7977.83). All paths below are relative to it
unless they start with `patches/` (the Arcium repo). Line numbers are from this tree.

Short answer: a fixed-partition SiteInstance does what Arcium needs. Content applies it to every
navigation in the tab's frame tree, including swaps to a new BrowsingInstance. It does not reach
new tabs made by Chrome, noopener popups, session restore or prerender, each of which builds a fresh
SiteInstance. The biggest danger is not a leak but data loss: Chrome's storage-partition garbage
collector deletes any partition directory it does not know about.

---

## 1. Mechanism

### 1.1 `SiteInstance::CreateForFixedStoragePartition` (the one that fits)

- Declared at `content/public/browser/site_instance.h:252-258`: "a SiteInstance in a new
  BrowsingInstance with a custom StoragePartition that is preserved across navigations".
  The partition must not be the default one.
- Implemented at `content/browser/site_instance_impl.cc:243-256`. It creates a UrlInfo carrying the
  config and calls `CreateForUrlInfo(..., is_fixed_storage_partition=true)`, which makes a new
  `BrowsingInstance` with that flag (`site_instance_impl.cc:148-172`).
- The flag lives on the BrowsingInstance and cannot change (`content/browser/browsing_instance.h:106-116`,
  `130-133`, `332-338`). The upstream TODO at `browsing_instance.h:335-337` (crbug.com/40943418) says
  "We actually always want this behavior". Upstream intends the partition to follow the BrowsingInstance.
- It is enforced in three places:
  - `SiteInstanceImpl::DeriveSiteInfo` overrides the UrlInfo's partition when the flag is set
    (`site_instance_impl.cc:830-835`).
  - `NavigationRequest` stamps the current SiteInstance's partition onto every navigation's UrlInfo
    (`content/browser/renderer_host/navigation_request.cc:4619-4625`). Child and fenced frames always
    take the parent's partition (`4627-4633`).
  - `RenderFrameHostManager`: when a navigation needs an *unrelated* SiteInstance (a new
    BrowsingInstance), it copies the partition and passes `IsFixedStoragePartition()` on to the new
    BrowsingInstance (`content/browser/renderer_host/render_frame_host_manager.cc:4064-4091`).
- The one upstream browser test is `content/browser/navigation_browsertest.cc:8255-8354`
  (`NavigationBrowserTest.FixedStoragePartition`). It covers same-tab navigation, `window.open` with an
  opener, `window.open()` then navigate, and a cross-BrowsingInstance navigation, and asserts the
  partition survives each one.
- The only production caller on any platform is Android's may-launch-URL experiment
  (`chrome/browser/android/content/web_contents_factory.cc:43-80`). It uses an in-memory partition and
  deletes the partition's data when the WebContents dies.
- **Lifetime: whole life of the WebContents' frame tree**, for every navigation the tab makes itself.
  It does not carry to *new* WebContents that someone else creates with a fresh SiteInstance (section 2).

### 1.2 `StoragePartitionConfig::Create(browser_context, domain, name, in_memory)`

- `content/public/browser/storage_partition_config.cc:29-40`. This only builds a value. It CHECKs that
  the domain is non-empty, and forces `in_memory` for off-the-record contexts. It does nothing unless a
  SiteInstance, guest or URL hook uses it.

### 1.3 `WebContents::CreateParams::site_instance` with an ordinary SiteInstance

- The comment says it only avoids a process swap on the first navigation (`content/public/browser/web_contents.h:200-203`).
- A normal SiteInstance takes its partition from the URL. `SiteInfo::Create` calls
  `GetStoragePartitionConfigForUrl` when the UrlInfo has no config (`content/browser/site_info.cc:335-338`),
  which asks the embedder (`site_info.cc:812-820`).
- **Lifetime: first navigation at most.** Any later navigation that picks a new SiteInstance goes back
  to the per-URL answer, which is the default partition.

### 1.4 The BrowsingInstance's own partition config

- `browsing_instance.h:323-330` and `browsing_instance.cc:174-188`: a BrowsingInstance locks to the
  partition of its first SiteInstance and CHECKs that the others match. `browsing_instance.cc:254-267`
  makes later SiteInstances inherit it.
- **Lifetime: one BrowsingInstance.** A cross-BrowsingInstance swap (typed URL to another site, COOP)
  starts a new one. Without the fixed flag, that new one takes the default partition.

### 1.5 Guests (`<webview>`, Controlled Frame, slim webview)

- `SiteInstance::CreateForGuest` (`site_instance_impl.cc:222-238`) always sets the fixed flag.
  It is used by `extensions/browser/guest_view/web_view/web_view_guest.cc:487-495` and
  `components/guest_view/browser/slim_web_view/slim_web_view_guest.cc:567`. The guest branch in
  `CreateNewWindow` also keeps the partition for noopener popups (`content/browser/web_contents/web_contents_impl.cc:5557-5563`).
- **Lifetime: whole life**, but the guest is an inner WebContents with an embedder, and `is_guest` is
  threaded through process, popup and prerender code. It is not a tab-strip tab. Not a fit.

### 1.6 Per-URL embedder hook (Isolated Web Apps, isolated-storage extensions)

- `ChromeContentBrowserClient::GetStoragePartitionConfigForSite`
  (`chrome/browser/chrome_content_browser_client.cc:1825-1869`) maps `chrome-extension://<id>` to that
  extension's partition (`1842-1849`, `extensions/browser/extension_util.cc:221-235`), and
  `isolated-app://` origins to the IWA's partition (`1851-1866`).
- The hook's signature is `(BrowserContext*, const GURL& site)` (`content/public/browser/content_browser_client.h:1204-1210`).
  It has no tab and no WebContents.
- **Lifetime: every navigation**, because the URL decides. That only works when the URL *is* the
  partition key (section 4).

---

## 2. Navigation paths, for a tab created with a fixed-partition SiteInstance

| Path | Keeps partition? | Evidence |
|---|---|---|
| (a) Omnibox, cross-site, same tab | Yes, including across a BrowsingInstance swap | `render_frame_host_manager.cc:4064-4091`; test `navigation_browsertest.cc:8336-8353` |
| (b) Renderer-initiated cross-site navigation, redirects | Yes | Every NavigationRequest reads the current SiteInstance's fixed partition (`navigation_request.cc:4619-4625`). Redirect-time recomputation goes through the same unrelated-SiteInstance path (`render_frame_host_manager.cc:4076-4091`). |
| (c1) `window.open` with an opener | Yes | The new WebContents reuses the opener's SiteInstance (`web_contents_impl.cc:5555-5567`, else branch); test `navigation_browsertest.cc:8288-8331` |
| (c2) `window.open(..., 'noopener')`, `rel=noopener`, `target=_blank` (implicit noopener), COOP-severed popups | **No, leaks to default** | Non-guest noopener gets `SiteInstance::Create(GetBrowserContext())` (`web_contents_impl.cc:5557-5566`), an ordinary BrowsingInstance with no fixed flag (`site_instance_impl.cc:139-145`). The first load deliberately omits `source_site_instance` (`web_contents_impl.cc:5752-5758`). |
| (d) Cmd/Ctrl+click, middle-click, context menu "Open link in new tab" | **No, leaks to default** | The click reaches `chrome::Navigate` with `source_site_instance` set (`content/browser/renderer_host/navigator.cc:1145`; `components/renderer_context_menu/render_view_context_menu_base.cc:546`). But `CreateTargetContents` uses the opener's SiteInstance only when `params.opener` is set, and otherwise uses `tab_util::GetSiteInstanceForNewTab` (`chrome/browser/ui/navigator/browser_navigator.cc:466-485`; `chrome/browser/tab_contents/tab_util.cc:22-40`). `source_site_instance` is documented as only mattering for about:blank and data: URLs (`browser_navigator_params.h:326-331`). The same applies to omnibox Alt+Enter, bookmarks, `chrome.tabs.create`, and DevTools "open in new tab". |
| (e) Back/forward, bfcache | Yes | A history navigation passes the FrameNavigationEntry's SiteInstance as `dest_instance` (`render_frame_host_manager.cc:3353`, `3614-3619`, `3884-3946`). That SiteInstance belongs to the fixed BrowsingInstance. The bfcache restores the same RenderFrameHost. |
| (f1) Prerender (omnibox, bookmark, speculation rules) | **No, and activation would swap the tab's page into the default partition** | `PrerenderHost` builds its frame tree on `SiteInstanceImpl::CreateForURL` or `::Create` (`content/browser/preloading/prerender/prerender_host.cc:522-527`), neither fixed. No prerender eligibility check looks at the partition (the only `kNonDefaultStoragePartition` producer is prefetch). The session-storage namespace is looked up by the *prerender's* partition (`prerender_host.cc:552-558`). Opt-out hook: `WebContentsDelegate::IsPrerender2Supported`, consulted at `prerender_host_registry.cc:615`, implemented by `Browser` at `chrome/browser/ui/browser.cc:1204-1210`. |
| (f2) Prefetch | Safe (refused) | Prefetch rejects a non-default initiator partition (`content/browser/preloading/prefetch/prefetch_service.cc:945-962`). The fast-fetch path honours the fixed flag (`content/browser/loader/navigation_fast_fetch_manager.cc:69-77`). |
| (g) Session restore, Cmd+Shift+T | **No, leaks to default** | Nothing persists a partition: no hits for storage partition in `components/sessions`. `CreateRestoredTab` uses `GetSiteInstanceForNewTab` and a default-partition session-storage map (`chrome/browser/ui/browser_tabrestore.cc:72-77`). Restored entries carry no SiteInstance, so the first load uses the WebContents' SiteInstance. Arcium already hooks this function (`patches/0120-restore-tab-entry.patch`, `arcium::StashRestoredEntryId` at `browser_tabrestore.cc:91`). |
| (h) Tab discard, then reload | Probably yes on desktop, not proven | `kWebContentsDiscard` is off on desktop (`content/public/common/content_features.cc:383-389`), so `FinishDiscard` builds a new WebContents with `CreateParams(profile)` and no SiteInstance (`chrome/browser/resource_coordinator/tab_lifecycle_unit.cc:274-285`), then `CopyStateFrom` (`content/browser/renderer_host/navigation_controller_impl.cc:3028-3049`). The copied entries use `CloneWithoutSharing` (`5215-5227`), and `FrameNavigationEntry::Clone` keeps `site_instance_` (`content/browser/renderer_host/frame_navigation_entry.cc:56-60`). The reload therefore navigates with `dest_instance` equal to the old fixed SiteInstance. If `CanUseDestinationInstance` refuses it (COOP/COEP change, error page; `render_frame_host_manager.cc:3884-3946`), the fallback is the new WebContents' non-fixed SiteInstance, which means default. Needs a test, or a hook in `FinishDiscard`. |
| Duplicate tab | Yes | `WebContentsImpl::Clone` reuses `GetSiteInstance()` (`web_contents_impl.cc:4250-4253`) |
| (i1) DevTools | Inspection is correct; "open in new tab" leaks like (d) | The DevTools front-end is its own WebContents, which is harmless. Storage inspection uses the inspected process's partition (`content/browser/devtools/protocol/storage_handler.cc:422`). |
| (i2) PDF viewer | Yes, with an extra process | PDF out-of-process iframes are on by default (`pdf/pdf_features.cc:33`). The viewer is a child frame, so it takes the parent's partition (`navigation_request.cc:4627-4633`). PDF and non-PDF processes never share (`content/browser/renderer_host/render_process_host_impl.cc:4942-4943`). |
| (i3) chrome-extension:// or chrome:// typed into such a tab | Stays in the custom partition, which is probably wrong | The fixed override beats the per-URL answer (`site_instance_impl.cc:830-835`). An extension page would get per-Arcium-profile localStorage and IndexedDB, and an isolated-storage extension would be pulled out of its own partition. I found no CHECK against this, but nothing upstream tests it. The same holds for WebUI such as the NTP. |
| (j) Service, shared and dedicated workers | Yes | Navigation looks up service workers in `site_info_`'s partition (`navigation_request.cc:2190-2198`). Service-worker processes are created with the partition config (`site_instance_impl.cc:175-218`, `content/browser/service_worker/service_worker_process_manager.cc:139`). Shared workers carry the fixed flag (`content/browser/worker_host/shared_worker_service_impl.cc:420-434`). Dedicated workers run in the frame's process. |
| Downloads | Yes | The partition comes from the frame's SiteInstance (`content/browser/download/download_manager_impl.cc:1550-1562`, `1832-1841`) and is serialised for resumption (`558-576`). |

---

## 3. Chrome-layer assumptions

- **CookieSettings and HostContentSettingsMap** are per Profile. Every partition's network context
  gets the same `cookie_manager_params` built from the profile's CookieSettings
  (`chrome/browser/net/profile_network_context_service.cc:1381-1382`). Site exceptions and the
  third-party-cookie policy are therefore shared across Arcium profiles, which is the intended design.
- **Network context for a custom partition.** `StoragePartitionImpl::InitNetworkContext` calls
  `ConfigureNetworkContextParams(browser_context, is_in_memory(), relative_partition_path_, ...)`
  (`content/browser/storage_partition_impl.cc:3500-3508`). That goes to
  `ChromeContentBrowserClient::ConfigureNetworkContextParams` (`chrome_content_browser_client.cc:6948-6960`)
  and then `ProfileNetworkContextService` (`profile_network_context_service.cc:619-636`, `1339-1464`).
  For an on-disk partition it sets the cookie DB, HTTP cache, network data, trust-token and reporting
  DBs under the partition path (`1386-1447`). The path is computed at `1644-1648`.
- **Cookie persistence across relaunch.** Persistent cookies do persist for an on-disk custom
  partition. **Session cookies do not**: any non-empty `relative_partition_path` forces
  `restore_old_session_cookies=false` and `persist_session_cookies=false` (`profile_network_context_service.cc:1449-1457`),
  even when the user has "continue where you left off" on. Sites that log in with a session cookie
  will be logged out after relaunch in every non-default Arcium profile.
- **Password manager and autofill** get their own network context from the default partition
  (`chrome/browser/password_manager/chrome_password_manager_client.cc:1354-1360`,
  `factories/profile_password_store_factory.cc:40`, `chrome/browser/ui/autofill/chrome_autofill_client.cc:389`).
  These are used for leak checks, affiliation and server calls, not for page cookies. Filling works per
  frame, so shared passwords work in every partition. No problem found.
- **Downloads**: correct, see the table above.
- **Site data UI.** `BrowsingDataModel::BuildFromDisk` reads only the default partition
  (`components/browsing_data/content/browsing_data_model.cc:660-667`). Settings uses it
  (`chrome/browser/ui/webui/settings/site_settings_handler.cc:2289`), so custom-partition data is
  invisible there. `BuildFromNonDefaultStoragePartition` exists (`browsing_data_model.cc:669-677`) and
  is what an Arcium per-profile site-data view would call.
- **Clear browsing data.** `BrowsingDataRemoverImpl` acts on the default partition unless the filter
  names one (`content/browser/browsing_data/browsing_data_remover_impl.cc:354-363`, `809-821`).
  To clear one partition: `BrowsingDataFilterBuilder::SetStoragePartitionConfig`
  (`content/public/browser/browsing_data_filter_builder.h:137-140`) plus `RemoveWithFilter` with the
  `DATA_TYPE_ON_STORAGE_PARTITION` mask. Chrome already does exactly this for every IWA partition in
  `ChromeBrowsingDataRemoverDelegate` (`chrome/browser/browsing_data/chrome_browsing_data_remover_delegate.cc:1421-1464`),
  which is the template for an Arcium loop. The delegate also hard-codes the default partition for
  several steps (`288`, `460`, `513`, `693`, `958`, `1342`). To wipe a whole Arcium profile, use
  `BrowserContext::AsyncObliterateStoragePartition(domain)` (`content/browser/browser_context.cc:~178-184`,
  `storage_partition_impl_map.cc:367-410`). It is domain-wide, which argues for one partition domain per Arcium profile.
- **Extensions**: `chrome.cookies` reads only the default partition (`chrome/browser/extensions/api/cookies/cookies_api.cc:121`, `278`).
  The spec already accepts this.
- **Partition garbage collection deletes unknown partitions.** When the pref
  `kShouldGarbageCollectStoragePartitions` is true, `WebAppProvider` schedules
  `GarbageCollectStoragePartitionsCommand` at startup (`chrome/browser/web_applications/web_app_provider.cc:576-579`).
  The pref is set by extension data deletion (`chrome/browser/extensions/data_deleter.cc:86`) and IWA
  removal (`chrome/browser/web_applications/isolated_web_apps/web_app_isolation_delegate_impl.cc:54`).
  The allowlist holds only isolated-storage extensions and IWAs
  (`chrome/browser/web_applications/commands/garbage_collect_storage_partitions_command.cc:64-87`,
  `extensions/extensions_manager_impl.cc:76-88`, `web_app_isolation_delegate_impl.cc:75-89`).
  `StoragePartitionImplMap::GarbageCollect` then deletes everything under `<profile>/Storage/ext/`
  that is neither allowlisted nor currently loaded (`content/browser/storage_partition_impl_map.cc:412-432`).
  Arcium partitions load lazily, and Arcium defers restored loads (`patches/0170-session-restore-defer-loads.patch`).
  An Arcium profile with no loaded tab at that moment would have its cookies and storage deleted.
  **This is the biggest risk found.**
- **Prefetch** refuses non-default partitions (`prefetch_service.cc:945-962`). Speculation-rule
  prefetch simply does not happen in those tabs. This costs performance, not correctness.
- In total there are 193 non-test `.cc` files under `chrome/browser` that call `GetDefaultStoragePartition()`.
  The ones above are those on page-data paths. Most of the rest are profile-level services, such as
  signin, sync and updaters, that correctly use the default partition.

---

## 4. Precedent

- **No production code on desktop puts ordinary tab-strip tabs into a non-default partition.** The
  only non-test `CreateForFixedStoragePartition` caller is the Android experiment (1.1).
- IWAs and isolated-storage Chrome Apps choose the partition **by URL** in
  `GetStoragePartitionConfigForSite` (1.6). They live in app windows, and
  `chrome/browser/web_applications/isolated_web_apps/isolated_web_app_throttle.cc` cancels
  `isolated-app://` navigations outside them (`:195`). Their partitions are what Chrome's clear-data
  loop and the garbage collector know about (section 3).
- Guests (webview, Controlled Frame) use the fixed flag through `CreateForGuest` (1.5).
- **Copying the per-URL pattern does not work** when the partition depends on the tab. The hook
  receives only `(browser_context, site)` (`content_browser_client.h:1204-1210`, `site_info.cc:812-820`).
  It is also called without any frame in hand, for example by downloads
  (`download_manager_impl.cc:641-643`) and prefetch (`BrowserContext::GetStoragePartitionForUrl`,
  `prefetch_service.cc:958`). The same site open in two Arcium profiles cannot be told apart by URL.
  Encoding the profile into the URL would change origins. The fixed-partition BrowsingInstance is the
  model upstream is moving towards (`browsing_instance.h:335-337`).

---

## 5. Cost of each extra on-disk partition

- **Created lazily, never released.** A partition is built on the first `GetStoragePartition(config)`
  (`storage_partition_impl_map.cc:326-365`) and not unloaded until the BrowserContext shuts down
  (`content/public/browser/browser_context.h:176-182`, and the comment at
  `chrome/browser/android/content/web_contents_factory.cc:70-73`). Closing a profile's last tab frees
  nothing. Nothing is created at startup unless a tab or restore asks for it.
- **Work at creation**, on the UI thread. `StoragePartitionImpl::Initialize`
  (`storage_partition_impl.cc:1391-1640`) builds about 17 sub-contexts: quota, filesystem, DOM storage,
  locks, cache storage, dedicated workers, push, content index, background fetch and sync, broadcast
  channel, blob registry and URL registry, bucket manager, font access, CDM storage, and others. The
  map then immediately forces the NetworkContext into existence to arm cookie-change listening
  (`storage_partition_impl_map.cc:359-362`). It also posts a WebSQL directory delete and starts
  background fetch (`452-464`). The histogram `Storage.StoragePartition.InitializeDuration` measures this (`storage_partition_impl.cc:1399`).
- **Network service.** No extra process. The partition adds one NetworkContext inside the existing
  network-service process, with its own cookie store, HTTP disk-cache backend, HttpServerProperties,
  transport-security persister, trust-token and reporting stores, and cert-verifier pipe
  (`profile_network_context_service.cc:1400-1464`, `storage_partition_impl.cc:3500-3530`).
  The code gives no memory figure. Acceptance item A3.3 requires measuring it.
- **Renderer processes.** A renderer process serves exactly one partition
  (`render_process_host_impl.cc:4945-4953`). The same site in two Arcium profiles means two
  processes, and service-worker processes are per partition too. The spare renderer is
  partition-specific and is thrown away on mismatch
  (`content/browser/renderer_host/spare_render_process_host_manager_impl.cc:604-619`), so the first
  navigation after switching to another profile's space misses the warm spare. Any WebUI or NTP forced
  into a custom partition gets its own process per partition.
- **Disk layout.** `<profile>/Storage/ext/<partition_domain>/<hex of first 6 bytes of SHA-256(partition_name)>`,
  or `.../def` when the name is empty (`storage_partition_impl_map.cc:55-75`, `120-126`, `300-315`).
  The inside mirrors a profile directory: Cookies, Network/, Local Storage, IndexedDB, Service Worker,
  and so on. The HTTP cache sits under the user cache directory derived from that path
  (`profile_network_context_service.cc:1388-1405`). In-memory configs write nothing.
  The `ext` directory name is historical, and it is the same tree the garbage collector sweeps.

---

## Recommended mechanism, what it needs hooked, and the paths that would leak to the default partition without a hook

**Mechanism.** Give each non-default Arcium profile
`StoragePartitionConfig::Create(profile, "arcium-<profile-id>", "", /*in_memory=*/false)`. Using one
domain per profile makes `AsyncObliterateStoragePartition` delete exactly one profile, and the
domain cannot collide with 32-letter extension ids. Create every tab of that profile with
`SiteInstance::CreateForFixedStoragePartition(profile, url, config)` as `CreateParams::site_instance`.
The default Arcium profile keeps the untouched Chromium path. Content then keeps the partition for
every navigation inside the tab's frame tree.

**Hooks needed** (each a thin call into `arcium/`):

1. **New tabs from Chrome**: `CreateTargetContents` (`browser_navigator.cc:479-485`) or
   `tab_util::GetSiteInstanceForNewTab`. Choose the fixed SiteInstance from the source tab's
   partition, or from the target space's profile. This one seam covers Cmd/Ctrl+click, middle-click,
   the context menu, Alt+Enter, bookmarks, `chrome.tabs.create`, DevTools "open in new tab" and Arcium's quick entry.
2. **Noopener popups**: `WebContentsImpl::CreateNewWindow` (`web_contents_impl.cc:5557-5566`). When
   the source SiteInstance is fixed, use `CreateForFixedStoragePartition(ctx, GURL(), partition_config)`,
   mirroring the guest branch just above it. This is a content patch, and content cannot call into
   `arcium/browser`, so the patch carries this one condition itself. It is the upstream TODO's
   behaviour. The chrome-side alternative (`Browser::IsWebContentsCreationOverridden` /
   `CreateCustomWebContents`, `browser.cc:1629-1690`) never learns `opener_suppressed`, so it cannot
   separate noopener popups from ordinary ones.
3. **Session restore and Cmd+Shift+T**: `CreateRestoredTab` (`browser_tabrestore.cc:75-77`), already
   hooked by patch 0120. Persist the profile id in the tab's extra_data next to the entry id, build the
   fixed SiteInstance there, and key the restored session-storage map to that partition.
4. **Prerender**: `Browser::IsPrerender2Supported` (`browser.cc:1204-1210`). Return
   `kPreloadingDisabled` for tabs whose SiteInstance is fixed. The alternative is a content fix in
   `PrerenderHost` (`prerender_host.cc:522-527`) that creates the prerender SiteInstance in the
   initiator's fixed partition.
5. **Partition garbage collection**: `GarbageCollectStoragePartitionsCommand::DoGarbageCollection`
   (`garbage_collect_storage_partitions_command.cc:64-87`). Add every Arcium partition path to the allowlist.
6. **Clear browsing data**: in `ChromeBrowsingDataRemoverDelegate`, loop over Arcium partitions the way
   `1421-1464` loops over IWAs, or rely on Arcium's own per-profile control.
7. **Session cookies**: `profile_network_context_service.cc:1449-1457`. Decide whether Arcium
   partitions follow the profile's session-cookie restore setting. Today they never restore session cookies.
8. **Discard**: `FinishDiscard` (`tab_lifecycle_unit.cc:274`). Either give the replacement WebContents
   the old tab's fixed SiteInstance, or prove with a browser test that the reload always reuses the
   entry's SiteInstance.
9. **Policy question**: what a chrome:// or chrome-extension:// URL does in a custom-partition tab. The
   existing home-boundary throttle (`patches/0150-navigation-throttle-home-boundary.patch`) could send
   such URLs to a default-profile tab.

**Paths that leak to the default partition without a hook:**

- noopener popups, including `target=_blank`
- every `chrome::Navigate` new tab: modified clicks, the context menu, the omnibox new-tab path, bookmarks, extension-created tabs and DevTools
- session restore and reopen-closed-tab
- prerender activation from the omnibox, bookmarks and speculation rules
- the discard-then-reload fallback case
- chrome:// and extension pages are the reverse case: they are pulled *into* the custom partition.

**Paths that need nothing:**

- same-tab navigation of every kind, including cross-BrowsingInstance swaps and redirects
- iframes and fenced frames
- `window.open` with an opener
- back/forward and the bfcache
- duplicate tab
- service, shared and dedicated workers
- downloads
- DevTools storage inspection
- the PDF viewer

**Data-loss risks independent of navigation:**

- the startup garbage collector (hook 5)
- session cookies dropped on relaunch (hook 7).
