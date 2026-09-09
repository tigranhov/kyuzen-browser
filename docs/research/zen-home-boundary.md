# Zen Browser source research: the pinned / essential home boundary

Repository: `zen-browser/desktop` (branch `dev`), shallow-cloned to
`/Volumes/Texternal/repositories/zen-desktop` — a sibling of this repo, never
inside it. Firefox paths are from `mozilla-firefox/firefox` (branch `main`),
fetched raw. Line numbers are from the checkout of 2026-09-09.

**The headline: Zen implements no home boundary of its own.** It inherits
Firefox's app-tab rule, unchanged. Everything below is therefore mostly a
description of Firefox behaviour that Zen switches on by pinning.

---

## 1. The rule itself

`nsDocShell::ShouldOpenInBlankTarget` (`docshell/base/nsDocShell.cpp:12342`),
whose comment names the bug it came from:

```cpp
// External links from within app tabs should always open in new tabs
// instead of replacing the app tab's page (Bug 575561)
```

In order, it refuses to redirect the navigation unless **all** of these hold:

| # | Condition | Consequence when it fails |
|---|---|---|
| 1 | the link URI is not `javascript:` | stays in the tab |
| 2 | the link URI has a host | stays in the tab |
| 3 | the anchor's `target` is **empty** — the default | a named or `_blank` target is left alone |
| 4 | the tab is an **app tab** (or a `webext-browsers` panel) | ordinary tabs are unaffected |
| 5 | the link's host **differs from the host of the document currently loaded** | same host stays in the tab |
| 6 | the difference is not merely a `www.` prefix on either side | `www.x.com` ↔ `x.com` stays in the tab |

The comparison is **host string equality**, not registrable domain, not
origin. Scheme and port are not consulted at all.

Firefox's own test, `browser/components/tabbrowser/test/browser/tabs/
browser_navigatePinnedTab.js`, asserts both halves and is worth reading as the
specification:

- typing a URL into the address bar and pressing Go **navigates the pinned tab
  in place** — `is(gBrowser.tabs.length, initialTabsNo, "No additional tabs
  were opened")`;
- `BrowserTestUtils.startLoadingURIString(...)` — a programmatic load — also
  stays in the tab;
- clicking an `<a href>` to a different host **opens a new tab**, and that tab
  has **no opener**: `is(content.opener, null, "No opener should be
  available")`.

An app tab is exactly a pinned tab. `Tabbrowser.sys.mjs:1214` and `:3339`:

```js
browser.browsingContext.isAppTab = aTab.pinned;
```

## 2. Why this survives sign-in, which is what the spec was afraid of

The Stage 2.6 spec section warns that a same-registrable-domain rule breaks
sign-in, "because `mail.google.com` to `accounts.google.com` is one session and
so is every OAuth hop and consent interstitial."

Firefox's rule is *stricter* than registrable domain — it compares hosts — and
yet it does not break sign-in, because **condition 3 and the call site do the
work the domain rule cannot.** `ShouldOpenInBlankTarget` is reached only from
`nsDocShell::OnLinkClick`. A server redirect, a `<meta>` refresh, a
`location.href =` assignment, a form POST, a typed URL and a session restore
are **not link clicks** and never consult it. An OAuth hop is a redirect, so it
stays in the tab no matter how far the host travels.

**The answer to "which navigations count as leaving" is therefore not a URL
policy at all. It is: a user's click on an anchor with a default target.**
That is a far narrower and more predictable seam than the spec anticipated, and
it needs no allow-list of auth hosts and no heuristic.

## 3. What Zen adds on top

Zen leaves the rule alone — a repo-wide search for `isAppTab`,
`ShouldOpenInBlankTarget` and `TargetTopLevelLinkClicksToBlank` finds only one
read, in Glance (§5). What Zen adds is an **indicator**, not a boundary.

`nsZenPinnedTabManager.onLocationChange`
(`src/zen/tabs/ZenPinnedTabManager.mjs:938`) runs on every top-level location
change and:

- returns immediately unless the tab is pinned **and not** `zen-essential` and
  has a stored `_zenPinnedInitialState.entry`;
- strips the fragment from both the stored pin URL and the current URL;
- if they match, clears `zen-pinned-changed`; otherwise sets it.

The attribute drives a reset affordance — `resetPinnedTab`,
`_onTabResetPinButton` (`:226`, `:118`), which restores the stored state, or
with Accel held **duplicates the tab first** and then resets the pinned one,
so the user keeps where they got to.

Two consequences worth stating plainly:

- **Essentials get no changed-indicator.** The early return excludes them.
- **Pinned tabs navigate freely in place** for everything that is not a
  cross-host link click. The indicator exists precisely because they do.

Arcium already has this half: `can_return_to_pinned_url` and the "Return to
pinned URL" menu item are `zen-pinned-changed` and its reset. **What Arcium
lacks is only the link-click boundary of §1.**

## 4. The five open questions, answered from the source

The spec recorded five questions for this stage's design session. Zen and
Firefox answer all of them.

1. **What is "home"?** Neither an origin, nor a registrable domain, nor the
   pinned URL. It is **the host of the document currently loaded in the tab**,
   compared to the link's host, with a `www.` allowance. Note the consequence:
   home *moves with you*. Follow a same-host link three pages deep and the
   boundary is measured from page three, not from the pinned URL.
2. **Do auth and consent hops get an allowance?** They need none. They are not
   link clicks. No list, no heuristic.
3. **Does the boundary apply to pinned entries too?** **Yes — to both.** In Zen
   an essential *is* a pinned tab (`TabState-sys-mjs.patch:20`:
   `tabData.pinned = tabData.pinned || tabData.zenEssential`), and `isAppTab`
   follows `pinned`. This confirms the project owner's instruction directly.
   It does not conflict with the spec's line 74: free in-place navigation plus
   a revert affordance is the *other* half, and Zen ships both together.
4. **Where does the new tab land?** An ordinary new tab, with its **opener
   nulled**. Zen can optionally route it into a Glance popup instead (§5),
   behind a pref, and that is out of scope for now.
5. **What happens to the favourite's own tab?** Nothing. It keeps showing the
   page it was showing. The click simply does not land on it.

## 5. Glance, deliberately out of scope

`gZenGlanceManager.shouldOpenTabInGlance`
(`src/zen/glance/ZenGlanceManager.mjs:1548`) can divert the spawned tab into a
popup preview when the owner is a pinned app tab and the domains differ:

```js
owner.pinned &&
  this._lazyPref.SHOULD_OPEN_EXTERNAL_TABS_IN_GLANCE &&
  owner.linkedBrowser?.browsingContext?.isAppTab &&
  this.tabDomainsDiffer(owner, uri)
```

`tabDomainsDiffer` (`:1503`) is again plain host inequality —
`Services.io.newURI(url1).host !== url2.host` — with no `www.` allowance, and
it treats an `about:` page as always different.

This is a *routing* decision layered on top of the boundary, not part of it.
The project owner has ruled popup and split modes unnecessary for this stage,
so the spawned tab is an ordinary tab here.

## 6. What this means for Arcium

Chromium has no app-tab concept, so the rule has to be built. The seam is the
delegate that already sees every navigation a tab asks for with a disposition:
`content::WebContentsDelegate::OpenURLFromTab`, reached in Chrome through
`Browser::OpenURLFromTab`. A patch there delegating into `arcium/` matches the
project's hook rule, and the decision — is this entry pinned or a favourite, is
this a user link click with a default target, do the hosts differ modulo `www.`
— is ordinary `arcium/` logic over `OpenURLParams`.

## 7. The Chromium mapping, probed 2026-09-09

Two questions were open when §6 was written. Both are now answered from the
Chromium tree at 152.0.7977.83, statically and without a build.

**The seam is a `NavigationThrottle`, not `OpenURLFromTab`.** A plain
same-frame link click never reaches `WebContentsDelegate::OpenURLFromTab`;
that path is for navigations carrying a disposition. Chrome already ships this
exact feature for tabbed web apps with a pinned home tab —
`web_app::TabbedWebAppNavigationThrottle`
(`chrome/browser/ui/web_applications/tabbed_web_app_navigation_throttle.cc`,
registered from `chrome_content_browser_client_navigation_throttles.cc:487`) —
and it is the template: filter in `MaybeCreateAndAdd`, decide in
`WillStartRequest`, and to divert, build
`OpenURLParams::FromNavigationHandle(...)`, set the disposition, clear
`frame_tree_node_id`, call `WebContents::OpenURL`, and return
`CANCEL_AND_IGNORE`. Its `WillRedirectRequest` carries a
`TODO(crbug.com/400761084)` admitting redirects are unresolved there; ours are
resolved by the rule below, since a redirect is not a link click.

**"A link click" needs no inference. Chromium already computes it and exposes
it.** `content::NavigationHandle::WasInitiatedByLinkClick()`
(`content/public/browser/navigation_handle.h:616`) is public API, backed by
`NavigationRequest::WasInitiatedByLinkClick`
(`navigation_request.cc:10432`), which reads a bit the renderer set in
`render_frame_impl.cc:6309` from Blink's navigation type.

That bit is **exactly** Firefox's `OnLinkClick` distinction. Blink's
`DetermineNavigationType` (`frame_loader.cc:614`) returns
`kWebNavigationTypeLinkClicked` when `have_event` is true, and `have_event` is
`request.GetTriggeringEventInfo() != kNotFromEvent`. A repo-wide search finds
only four places that ever set a triggering event:

- `html_anchor_element.cc:422` — an `<a href>` click
- `svg_a_element.cc:211` — an SVG `<a>`
- `mathml_anchor_element.cc:159` — a MathML `<a>`
- `form_submission.cc:374,463` — form submits, which are classified
  `kWebNavigationTypeFormSubmitted` first, because `is_form_submission` is
  tested before `have_event`

`Location::SetLocation` sets none. So `location.href = ...`,
`location.assign()` and `location.replace()` are **not** link clicks **even
inside a click handler**, and neither are `<meta>` refreshes, server
redirects, typed URLs or session restores. The failure the spec feared — a
"Continue with Google" button that assigns `location.href` being torn out of
the tab — cannot happen, for the same structural reason it cannot happen in
Firefox.

The full mapping, then:

| Firefox condition | Arcium / Chromium |
|---|---|
| reached only from `OnLinkClick` | `handle.WasInitiatedByLinkClick()` |
| not `javascript:` | scheme check on the target URL |
| link URI has a host | `url.has_host()` |
| default target only | `target="_blank"` lands in a *new* `WebContents`, so the pinned tab sees no navigation at all |
| tab is an app tab | our own model: the tab is bound to a pinned or favourite entry |
| link host ≠ current document host | `handle.GetURL().host()` vs `web_contents->GetLastCommittedURL().host()` |
| `www.` allowance | the same string comparison |
| — | plus, as Chrome's throttle does: primary main frame only, skip reloads |

**Nulling the opener** remains a detail to get right rather than a risk:
Firefox's test asserts `content.opener === null`, so the diverted tab must be
opened without an opener rather than inheriting one.

**Not yet confirmed empirically.** The evidence above is static. Before the
rule ships, one run against a real sign-in flow is worth the minutes it costs
— the claim being tested is that no OAuth hop arrives with
`WasInitiatedByLinkClick()` true.
