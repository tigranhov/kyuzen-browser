# Zen Browser source research: URL bar and extensions

Repository: `zen-browser/desktop` (default branch `dev`, a Firefox fork). All
file paths below are relative to the repo root. Raw content fetched from
`https://raw.githubusercontent.com/zen-browser/desktop/dev/<path>`, browsable
at `https://github.com/zen-browser/desktop/blob/dev/<path>`. Code search used
`gh api search/code` (authenticated GitHub CLI), which is more reliable than
GitHub's web code search for this task.

Zen's own architecture is close to Arcium's: upstream Firefox files are
touched only via numbered `.patch` files under `src/browser/`, `src/toolkit/`
etc.; new Zen logic lives in `src/zen/`; new styling lives in
`src/zen/common/styles/*.css` and small `.inc.css`/`.inc.xhtml` fragments
spliced into upstream markup by the patches.

---

## 1. What I could verify

### 1.1 The URL bar is a modified version of Firefox's `UrlbarInput`, not a rewrite

Source: `src/browser/components/urlbar/content/UrlbarInput-mjs.patch`
(https://github.com/zen-browser/desktop/blob/dev/src/browser/components/urlbar/content/UrlbarInput-mjs.patch)

Zen patches Firefox's stock `UrlbarInput.mjs` in place. The stock "breakout"
mechanism (a compact bar that grows into a taller, dropdown-anchored box on
focus) is kept; Zen adds a second state on top of it: floating.

### 1.2 Two distinct focus results: inline "breakout" vs. floating overlay

Quoted from the patch, in `startLayoutExtend()`:

```js
this.window.gZenUIManager.onUrlbarOpen();
if (this.zenUrlbarBehavior == 'float' || (this.zenUrlbarBehavior == 'floating-on-type' && !this.focusedViaMousedown)) {
  this.setAttribute("zen-floating-urlbar", "true");
  this.window.gZenUIManager.onFloatingURLBarOpen();
} else {
  this.removeAttribute("zen-floating-urlbar");
}
```

`zenUrlbarBehavior` reads the pref `zen.urlbar.behavior`, declared with
`XPCOMUtils.defineLazyPreferenceGetter(lazy, "ZEN_URLBAR_BEHAVIOR", "zen.urlbar.behavior", 'default')`.
Its default value, from `prefs/zen/zen-urlbar.yaml`:

```yaml
- name: zen.urlbar.behavior
  value: floating-on-type
```

So out of the box the bar floats **only when it is NOT focused by a mouse
click** — i.e. focused via keyboard (Cmd/Ctrl+L, a keyboard shortcut, or the
"Browser:OpenLocation" command). A plain mouse click on the bar leaves it
inline (`breakout-extend` only, no `zen-floating-urlbar`).

This is enforced again in `_on_mousedown()`:

```js
const isProbablyFloating =
  (this.zenUrlbarBehavior == "floating-on-type" &&
  this.hasAttribute("breakout-extend") && !this.focusedViaMousedown) ||
  (this.zenUrlbarBehavior == "float") || this.window.gZenVerticalTabsManager._hasSetSingleToolbar;
if (event.type != "click" && isProbablyFloating || event.type == "click" && !isProbablyFloating) {
  return true;
}
```

Confirmed by the browser test `src/zen/tests/urlbar/browser_floating_urlbar.js`:

```js
add_task(async function test_Floating_Urlbar() {
  gURLBar.blur();
  await SimpleTest.promiseFocus(window);
  document.getElementById("Browser:OpenLocation").doCommand();
  ...
  ok(gURLBar.hasAttribute("zen-floating-urlbar"), "URL bar should be in floating mode");
});

add_task(async function test_Click_Shoudnt_FLoat_Urlbar() {
  gURLBar.blur();
  await simulateClick(window);
  ...
  ok(!gURLBar.hasAttribute("zen-floating-urlbar"), "URL bar should not be in floating mode");
});

add_task(async function test_Floating_Highlight_Everything() {
  ...
  document.getElementById("Browser:OpenLocation").doCommand();
  Assert.equal(gURLBar.selectionStart, 0, "Selection start should be 0");
  Assert.equal(gURLBar.selectionEnd, gURLBar.value.length, "Selection end should be the length of the value");
});
```

So: keyboard-triggered focus also selects the entire URL text (like Arc/most
browsers' Cmd+L). Mouse-click focus does not float and does not select-all
(it places a caret, standard text-field behavior).

### 1.3 What the floating state looks like

`src/zen/common/styles/zen-omnibox.css`
(https://github.com/zen-browser/desktop/blob/dev/src/zen/common/styles/zen-omnibox.css):

```css
#urlbar[open][zen-floating-urlbar="true"] {
  z-index: 1000;
  max-width: unset;
  --urlbar-container-height: 62px !important;
  --urlbar-margin-inline: 12px !important;
  min-width: min(90%, 62rem) !important;
  width: var(--zen-urlbar-width, min(90%, 62rem)) !important;
  font-size: 1.15em !important;
  @media (-moz-platform: macos) { font-size: 1.5em !important; }
  top: var(--zen-urlbar-top) !important;
  translate: -50% 0%;
  left: 50% !important;
  ...
}
```

and in `.urlbar-background` under `#urlbar[breakout-extend]`:

```css
& .urlbar-background {
  --zen-urlbar-background-base: light-dark(#fbfbfb, color-mix(in srgb, hsl(0, 0%, 6.7%), var(--zen-colors-primary) 30%));
  @media -moz-pref("zen.theme.acrylic-elements") and (not (prefers-reduced-transparency: reduce)) {
    --zen-urlbar-background-transparent: light-dark(
      color-mix(in srgb, white 80%, transparent),
      color-mix(in srgb, var(--zen-urlbar-background-base) 65%, transparent)
    );
  }
  background-color: var(--zen-urlbar-background-transparent, var(--zen-urlbar-background-base)) !important;
  box-shadow: 0 30px 140px -15px light-dark(rgba(0, 0, 0, 0.8), rgba(0, 0, 0, 0.6)) !important;
  backdrop-filter: none !important;
  outline: 0.5px solid light-dark(rgba(0, 0, 0, 0.2), rgba(255, 255, 255, 0.2)) !important;
  ...
}
&, .urlbar-background { border-radius: 12px !important; }
```

`ZenUIManager.updateTabsToolbar()` positions it (`src/zen/common/modules/ZenUIManager.mjs`):

```js
updateTabsToolbar(fromResizeEvent = false) {
  const kUrlbarHeight = 333;
  gURLBar.style.setProperty("--zen-urlbar-top",
    `${window.innerHeight / 2 - Math.max(kUrlbarHeight, ...getBoundsWithoutFlushing(gURLBar).height) / 2}px`);
  gURLBar.style.setProperty("--zen-urlbar-width", `${Math.min(window.innerWidth / 1.5, 750)}px`);
  ...
}
```

So: the floating bar is horizontally centered (`left: 50%`, `translate(-50%, 0)`),
vertically centered relative to the window (computed against a fixed
`kUrlbarHeight = 333` budget so the results dropdown below it stays
centered too), width capped at `min(90%, 62rem)` (and additionally at
`innerWidth / 1.5`, max 750px, via the JS), larger font, a soft/light
background with an optional translucent "acrylic" backdrop-filter behind an
opt-in pref (`zen.theme.acrylic-elements`), a large soft drop shadow, a thin
1px outline, and 12px corner radius. It sits above everything (`z-index: 1000`).

### 1.4 Resting / inline (non-floating) state

Also in `zen-omnibox.css`, the un-floated bar is much flatter and lives
inside the sidebar's top toolbar row: `border-radius: calc(var(--border-radius-medium) - 2px)`,
background is `var(--zen-toolbar-element-bg)` (a plain surface color, no
shadow, no blur), height governed by `--urlbar-container-height: 48px`. On
hover (not focus) it just swaps to a slightly darker background:

```css
#urlbar:not([breakout-extend]) {
  &:hover .urlbar-background {
    background-color: var(--zen-toolbar-element-bg-hover) !important;
  }
}
```

### 1.5 Where the bar lives relative to the sidebar

Zen has two toolbar layouts, controlled by `zen.view.use-single-toolbar`
(default true, i.e. "single toolbar" is the out-of-the-box layout):

- **Single toolbar** (default): the classic Firefox navigation toolbar
  (`#nav-bar`, which contains the URL bar) is physically moved, at
  runtime, into the same DOM row as the sidebar's top button row
  (`zen-sidebar-top-buttons`) and the tab strip. Evidence in
  `src/zen/common/modules/ZenUIManager.mjs`:

  ```js
  this._hasSetSingleToolbar = true;
  ...
  buttonsTarget.prepend(document.getElementById("unified-extensions-button"));
  const panelUIButton = document.getElementById("PanelUI-button");
  buttonsTarget.prepend(panelUIButton);
  ...
  titlebar.parentNode.moveBefore(topButtons, titlebar);
  titlebar.parentNode.moveBefore(navBar, titlebar);
  ...
  document.documentElement.setAttribute("zen-single-toolbar", true);
  ```

  and the markup patch `src/browser/base/content/navigator-toolbox-inc-xhtml.patch`
  shows the URL bar's `#nav-bar` toolbar sitting directly under the same
  `<hbox id="titlebar">` as the tab strip, i.e. it is part of the sidebar
  column, at the top, not floating over content and not a separate
  horizontal bar above the page. It is **not** the classic two-row Firefox
  layout (menu/tabs row + separate nav-bar row above the page).

- **Double toolbar** (`zen.view.use-single-toolbar = false`): the classic
  Firefox arrangement is restored — `#nav-bar` (with the URL bar) goes back
  to `this._toolbarOriginalParent`, i.e. a conventional horizontal toolbar
  above the content area, separate from the sidebar. Confirmed by
  `src/zen/tests/urlbar/browser_single_toolbar_blur_revert.js`, which
  toggles `zen.view.use-single-toolbar` and asserts `gZenVerticalTabsManager._hasSetSingleToolbar`
  flips accordingly.

  The same test shows a real behavioural difference tied to layout: in
  single-toolbar mode, text typed into the bar is **reverted** to the page
  URL on blur if you don't navigate (Arc-style: the bar always "rests" on
  the current page URL); in double-toolbar mode the typed text is kept on
  blur (classic Firefox behaviour, since the two rows don't force
  bar-as-a-status-display semantics).

  ```js
  // single-toolbar
  await TestUtils.waitForCondition(() => gURLBar.value !== TYPED_VALUE,
    "The address bar should revert away from the typed value on blur");
  Assert.ok(gURLBar.value.includes("example.com"), ...);

  // double-toolbar
  Assert.equal(gURLBar.value, TYPED_VALUE,
    "Double-toolbar blur keeps the typed value (no forced revert)");
  ```

  So "floating on type" and "revert-on-blur" are two independently
  observed, separately-triggered behaviours, both defaults, both tied to
  layout/behavior prefs.

### 1.6 What Zen removes/hides from the stock omnibox

All from `src/zen/common/styles/zen-omnibox.css` unless noted:

```css
/* These are buttons that we dont need to be
 * displayed anymore, since now zen displays
 * them into a single, unified button */
#reader-mode-button,
.urlbar-go-button,
#star-button-box,
#pageActionButton:not([open]) {
  display: none !important;
}
```
— the reader-mode toggle, the "go" arrow button, the bookmark-star button,
and the generic page-action overflow button are all folded into one new
consolidated button (see 1.7/2 below — the "site data" button).

```css
@media not -moz-pref("zen.urlbar.show-pip-button") {
  #picture-in-picture-button { display: none !important; }
}
@media not -moz-pref("zen.urlbar.show-protections-icon") {
  #tracking-protection-icon-container { display: none !important; }
}
@media not -moz-pref("zen.urlbar.show-contextual-id") {
  #userContext-icons { display: none !important; }
}
```
with prefs (`prefs/zen/zen-urlbar.yaml`):
```yaml
- name: zen.urlbar.show-protections-icon
  value: false
- name: zen.urlbar.show-contextual-id
  value: false
- name: zen.urlbar.show-pip-button
  value: false
```
So by default the shield/tracking-protection icon, the container-tab
("contextual identity") color icon, and the picture-in-picture button are
all hidden, unless the user opts back in via prefs.

Search one-off buttons (the row of alternate-search-engine icons shown in
the results dropdown) are force-hidden regardless of stock logic. Patch
`src/browser/components/search/SearchOneOffs-sys-mjs.patch`:

```js
-    let hideOneOffs = (await this.willHide()) && !addEngineNeeded;
+    let hideOneOffs = (await this.willHide()) && !addEngineNeeded || Services.prefs.getBoolPref("zen.urlbar.hide-one-offs");
```
with pref default:
```yaml
- name: zen.urlbar.hide-one-offs
  value: true
```
Also, `zen-omnibox.css`:
```css
#urlbar {
  & .search-panel-one-offs-header { display: none; }
  & .search-panel-one-offs-container .searchbar-engine-one-off-item { box-shadow: none; }
}
```

**On the "Google icon" / "AI mode" affordance specifically**: I found no
equivalent in Zen at all — Firefox's stock omnibox never had a persistent
branded search-engine icon or an "AI mode" toggle inside the URL bar the
way Chromium's omnibox does, so there's nothing for Zen to remove there.
What Zen *does* hide is Firefox's own equivalent clutter: the one-off
search-engine icon row, the shield icon, the contextual-identity icon, and
consolidates the star/reader-mode/page-action buttons into one icon. This
is the closest verified analogue to "decluttering the bar of built-in
affordances."

### 1.7 The "single unified button" that replaces star/reader/page-action/site-info/extensions

`src/zen/urlbar/ZenSiteDataPanel.sys.mjs`
(https://github.com/zen-browser/desktop/blob/dev/src/zen/urlbar/ZenSiteDataPanel.sys.mjs):

```js
const ADDONS_BUTTONS_HIDDEN = Services.prefs.getBoolPref(
  "zen.theme.hide-unified-extensions-button", true
);
...
#init() {
  const button = this.window.MozXULElement.parseXULToFragment(`
    <box id="zen-site-data-icon-button" role="button" align="center" class="identity-box-button" delegatesanchor="true">
      <image />
      <image class="zen-site-data-boost-animation" />
    </box>
  `);
  this.anchor = button.querySelector("#zen-site-data-icon-button");
  this.document.getElementById("identity-icon-box").before(button);
  ...
  this.window.gUnifiedExtensions._button = ADDONS_BUTTONS_HIDDEN
    ? this.anchor
    : this.extensionsPanelButton;
  this.document.getElementById("nav-bar")
    .setAttribute("addon-webext-overflowbutton", "zen-site-data-icon-button");
  ...
}
```

Default pref (`prefs/zen/theme.yaml`):
```yaml
- name: zen.theme.hide-unified-extensions-button
  value: true
```
and CSS (`src/zen/common/styles/zen-single-components.css`):
```css
@media -moz-pref("zen.theme.hide-unified-extensions-button") {
  #unified-extensions-button:not([showing]) { display: none !important; }
}
```

So a new element, `#zen-site-data-icon-button`, is inserted directly into
the address bar's identity area (right before the lock/site-identity icon),
and by default the classic separate puzzle-piece "manage extensions" button
(`#unified-extensions-button`) is hidden. This new button opens a Zen-built
panel (`this.unifiedPanel`, using Firefox's own `unified-extensions-view`
content) that combines: site permissions/protections, cookie/storage info,
share/bookmark actions, AND the list of installed extensions — i.e. Zen
merged "page actions + site info + extensions management" into one icon
and one panel, anchored at the address bar rather than as a separate
toolbar button.

---

## 2. Extensions

### 2.1 Pinned extensions render inline next to the URL bar, invisible until hover

`src/zen/common/modules/ZenUIManager.mjs`, `appendCustomizableItem()`:

```js
appendCustomizableItem(target, child, placements = []) {
  if (this._hasSetSingleToolbar &&
      (target.id === "zen-sidebar-top-buttons-customization-target" ||
       target === this._topButtonsSeparatorElement)) {
    if (placements.includes(child.id)) {
      this._topButtonsSeparatorElement.before(child);
      return;
    } else if (
      child.hasAttribute("data-extensionid") &&
      Services.prefs.getBoolPref("zen.view.overflow-webext-toolbar", true)
    ) {
      if (gURLBar._isOverflowingItems) {
        const overflowElements = document.getElementById("zen-overflow-extensions-list");
        overflowElements.appendChild(child);
      } else {
        const element = document.getElementById("page-action-buttons");
        child.setAttribute("context", "toolbar-context-menu");
        element.before(child);
      }
      return;
    }
  }
  ...
}
```

So: any extension icon pinned to the toolbar (`data-extensionid` present)
is placed directly before `#page-action-buttons`, i.e. **inside the URL
bar's own row**, right next to the identity/page-action icons — as long as
there's room. If the bar is too narrow (`gURLBar._isOverflowingItems`),
that icon instead goes into a separate overflow container,
`#zen-overflow-extensions-list`.

Visibility of those inline icons (`.unified-extensions-item`, along with
`.identity-box-button` and `.urlbar-page-action`) is gated by CSS in
`zen-omnibox.css`:

```css
:root[zen-single-toolbar="true"] #urlbar:not([breakout-extend]) {
  ...
  .identity-box-button,
  .urlbar-page-action,
  .unified-extensions-item {
    opacity: 0;
    height: 100%; /* To still be able to open popups */
    visibility: collapse;
    margin: 0 !important;

    :root:not([supress-primary-adjustment="true"]) & {
      transition: opacity 0.15s, visibility 0.15s;
    }

    #navigator-toolbox[zen-has-implicit-hover="true"] &,
    &[open],
    #urlbar[has-popup-open="true"] &,
    :root[zen-has-empty-tab="true"] & {
      opacity: 1;
      visibility: visible;
    }
  }
}
```

**This directly matches your "hover reveals a toolbar of icons" mental
model.** By default, pinned-extension icons living in the URL bar row are
invisible (`opacity: 0`, `visibility: collapse`) — they take zero visual
space but stay hit-testable for their own open popups. They fade in
(0.15s) when any of: the surrounding `#navigator-toolbox` gets the
`zen-has-implicit-hover` attribute, the icon's own popup is `[open]`, the
URL bar itself has an open popup, or the current tab is an empty/new tab.

### 2.2 What sets `zen-has-implicit-hover`, and what "hover" region triggers it

`src/zen/compact-mode/ZenCompactMode.mjs`, `_setElementExpandAttribute()`:

```js
_setElementExpandAttribute(element, value, attr = "zen-has-hover") {
  ...
  if (value) {
    if (attr === "zen-has-hover" && element !== gZenVerticalTabsManager.actualWindowButtons) {
      element.setAttribute("zen-has-implicit-hover", "true");
      if (!lazy.COMPACT_MODE_SHOW_SIDEBAR_AND_TOOLBAR_ON_HOVER) {
        return;
      }
    }
    element.setAttribute(attr, "true");
    ...
```

and the hoverable-element list (`get hoverableElements()`):

```js
get hoverableElements() {
  return [
    { element: this.sidebar, screenEdge: ..., keepHoverDuration: ... },
    { element: document.getElementById("zen-appcontent-navbar-wrapper"), screenEdge: "top" },
    { element: gZenVerticalTabsManager.actualWindowButtons },
  ];
}
```

`addMouseActions()`, which wires `mouseover`/`mouseleave` listeners onto
these elements, is called unconditionally from `init()` — i.e. this hover
system runs at all times, **not only when Zen's separate "Compact Mode"
(an auto-hide-sidebar feature, off by default) is enabled.** So: hovering
the top toolbar wrapper (`zen-appcontent-navbar-wrapper`, the row that
contains the URL bar in single-toolbar mode) is enough to set
`zen-has-implicit-hover` on `#navigator-toolbox`, which fades in the
pinned-extension row, the identity box button, and other page-action icons
next to the bar. There's a short debounce (`HOVER_HACK_DELAY`, pref
`zen.view.compact.hover-hack-delay`, default `0`) to avoid flicker, and a
similar debounce/settle on leave.

I could not find a delay/animation duration constant specific to *this*
fade-in beyond the CSS `transition: opacity 0.15s, visibility 0.15s`
quoted above — there is no separate configurable "show after Nms" delay
for the icon reveal itself (unlike the sidebar's own auto-hide, which does
have configurable hold/hide durations, e.g. `zen.view.compact.toolbar-hide-after-hover.duration`,
`zen.view.compact.sidebar-keep-hover.duration` — those govern the
sidebar-as-a-whole in Compact Mode, a different, opt-in feature, not the
inline-icon reveal described above).

### 2.3 Overflow: a second, explicit "more icons" container above the tab strip

`src/browser/base/content/navigator-toolbox-inc-xhtml.patch`:

```xhtml
<hbox id="titlebar">
  <html:div id="zen-toolbar-background" ...>...</html:div>
  <box id="zen-overflow-extensions-list" skipintoolbarset="true" contextmenu="toolbar-context-menu" />
  <toolbar id="TabsToolbar" ...>
```

`src/browser/base/content/browser-addons-js.patch` shows this container is
also fed by Firefox's own toolbar-overflow machinery when the whole nav-bar
is too narrow to hold pinned items (`onToolbarVisibilityChange`, patched to
take an explicit `panel` argument so it can also target Zen's panel, not
just the stock one).

`src/zen/common/styles/zen-overflowing-addons.css`:

```css
#zen-overflow-extensions-list {
  display: none;
  :root[zen-single-toolbar="true"] &:not(:empty) {
    display: grid;
    gap: 8px;
    padding: 8px 2px;
    padding-bottom: 0;
    grid-template-columns: repeat(auto-fit, minmax(32px, 1fr));
    ...
  }
}
```

So this is a small grid of square icon buttons, placed in its own row just
above the tab strip (not inside the URL bar), that only appears
(`display: grid`, driven by `:not(:empty)`) once it actually has overflowed
extension icons in it — it is not itself gated by the hover attribute, so
once populated it is visible at rest (unlike the inline row next to the
identity icon, which is hover-gated).

Related prefs (`prefs/zen/view.yaml`):
```yaml
- name: zen.view.overflow-webext-toolbar
  value: true
- name: zen.view.overflow-webext-toolbar-threshold
  value: 55
```

### 2.4 Pinning/unpinning is stock Firefox, just re-anchored

`src/browser/base/content/browser-addons-js.patch` shows Zen did **not**
build a new pin/unpin mechanism. It patches the anchor/position of the
existing panel and its open/close plumbing (`togglePanel`, `panelUIPosition`,
`getPopupAnchorID` now returns `"zen-site-data-icon-button"` instead of
`"unified-extensions-button"`), but the actual pin action is untouched
stock code:

```js
async pinToToolbar(widgetId, shouldPinToToolbar) {
  let newArea = shouldPinToToolbar ? CustomizableUI.AREA_NAVBAR : CustomizableUI.AREA_ADDONS;
  let newPosition = shouldPinToToolbar ? undefined : 0;
  await gZenVerticalTabsManager._preCustomize();
  CustomizableUI.addWidgetToArea(widgetId, newArea, newPosition);
  await gZenVerticalTabsManager._postCustomize();
}
```

This is Firefox's stock "Pin to Toolbar" / "Remove from Toolbar" toggle,
normally reached via a per-extension context/kebab menu inside the
extensions panel. Zen's only change is *where that panel is opened from*.

`src/zen/urlbar/ZenSiteDataPanel.sys.mjs` further shows the same panel
content (`unified-extensions-view`, i.e. Firefox's real extensions list
markup) is reused verbatim and just re-anchored to the new
`#zen-site-data-icon-button`:

```js
this.unifiedPanel = this.#initUnifiedPanel();
this.unifiedPanelView = "unified-extensions-view";
...
if (ADDONS_BUTTONS_HIDDEN) {
  this.window.gUnifiedExtensions._panel = this.unifiedPanel;
  this.document.getElementById("unified-extensions-panel-template")?.remove();
}
```

Verified by `src/zen/tests/site_control/browser_site_control_shows_installed_addons.js`,
which opens `siteDataPanel()` and asserts installed extensions
(`.unified-extensions-item[data-extensionid]`) render inside it by name,
and disappear again once uninstalled.

### 2.5 No Zen-specific "extensions toolbar" component name

I did not find a file or class literally named `zen-extensions-toolbar` or
similar. The three relevant pieces are named:
- `src/zen/urlbar/ZenSiteDataPanel.sys.mjs` — the merged site-info/extensions panel and its trigger button (`#zen-site-data-icon-button`).
- `#zen-overflow-extensions-list` (declared inline in `navigator-toolbox-inc-xhtml.patch`, styled in `src/zen/common/styles/zen-overflowing-addons.css`) — the above-tab-strip overflow grid.
- The hover-reveal behaviour for pinned, inline extension icons is generic sidebar/toolbar hover plumbing in `src/zen/compact-mode/ZenCompactMode.mjs`, applied to those icons via CSS selectors in `src/zen/common/styles/zen-omnibox.css` — there is no dedicated "extensions hover" module; it rides the same mechanism used for other identity-box/page-action buttons.

---

## 3. What I could NOT verify

- **Exact fade timing / easing beyond the raw CSS.** I found the CSS
  transition (`opacity 0.15s, visibility 0.15s`) and the debounce constant
  name (`zen.view.compact.hover-hack-delay`, default `0`), but I did not
  run the browser to confirm perceived feel, and I did not find a
  Zen-specific "reveal delay" distinct from that debounce — it's possible
  there's additional smoothing I'm not seeing in static source (e.g. CSS
  custom easing curves layered elsewhere, or JS-side rAF batching effects
  on perceived timing).
- **Whether `#zen-overflow-extensions-list` is itself ever hover-gated.**
  The CSS I found only gates it on non-empty, not on hover — but I did not
  exhaustively check every stylesheet for a rule that might additionally
  hide it until hover (I checked `zen-overflowing-addons.css` and did a
  targeted `gh` code search for its id across the whole repo and found
  only the four files listed in section 2.3/2's search — reasonably
  confident, not exhaustive by manual read of every CSS file).
  Also I did not verify how `-webext-overflowtarget`/threshold pref
  (`zen.view.overflow-webext-toolbar-threshold: 55`, presumably pixels)
  actually decides when items move from inline to overflow — I found the
  pref and its consumption point in `appendCustomizableItem` refers to
  `gURLBar._isOverflowingItems`, but I did not trace how that boolean is
  computed against the threshold value.
- **Visual appearance in practice.** Everything above is read from source
  (CSS rules, JS logic, tests), not from a running build or screenshots —
  I have no visual/screen-capture confirmation of exact spacing, icon
  sizes, or how it actually looks composed together. The spec above is a
  reconstruction from rules, which should be accurate for *behavior* but
  could have subtleties (z-index stacking with other popups, RTL layout,
  Linux/Windows/macOS platform differences beyond the couple of
  `@media (-moz-platform: ...)` rules quoted) that only show up when run.
- **Firefox's stock unified-extensions panel UI itself** (its own
  pin/unpin affordance — a pin icon vs. a context-menu item, exact label
  text) — I traced Zen's re-anchoring of it but did not pull the stock
  Firefox `unified-extensions-item` markup/CSS to describe exactly how a
  user pins an item inside that panel (button placement, icon). That's
  genuine stock-Firefox UI, reachable from `browser/components/extensions/`
  in mozilla-central, which I did not fetch — out of scope of the Zen
  repo, but worth knowing if you want that level of detail too.
- **"AI mode" / Google icon**: confirmed absent conceptually (Firefox has
  no such affordance to begin with), but I did not do a negative-space
  search of the *entire* Zen CSS/JS tree for some experimental or
  discussion-only feature along these lines (e.g. an AI sidebar toggle
  elsewhere in the browser chrome, unrelated to the URL bar) — that was
  out of scope for "inside the bar" but flagging the boundary.

---

## 4. Behaviour spec (Chromium-Views-ready, no Firefox vocabulary)

### URL / address bar

**Resting state (bar not focused).**
The address bar sits at the top of the sidebar column, in the same row as
the sidebar's top button cluster and the tab strip — not as a separate
horizontal bar above the page content, and not floating. It is a flat,
low-contrast pill: quiet fill color, no border, no shadow, no visible
outline. It shows only the current page's title/URL text and, to its
trailing side, a compact identity icon (site lock/info). Hovering it
(without clicking) does nothing but a subtle background darken — it does
not expand.

Several affordances that a stock Chromium build shows inside or beside the
bar are removed from this resting state entirely, reappearing only inside
a single consolidated icon (see "Consolidated actions button" below): a
reader-view toggle, a bookmark/star toggle, a generic page-action overflow
chevron, and a "go" arrow. Three more are hidden by default outright,
recoverable only via a settings toggle: a tracking-protection/shield icon,
a container-tab color badge, and a picture-in-picture toggle. Alternate
search-engine suggestion icons (the row you'd see below the bar while
typing a query, offering "search this text on engine X/Y/Z") are
suppressed unconditionally by default, regardless of typed content.

A cluster of extension icons may also live inside this same row, to the
address bar's leading edge next to its page-action icons — but by default
they render at zero opacity and are not clickable-looking (though still
technically present) until the user's pointer enters the surrounding
toolbar area (see "Hover reveal" below). If there isn't enough width to
fit them, they don't get cut off or scroll — instead they relocate as a
small grid of icons in a thin strip directly above the tab list, which
(once it has anything in it) is always visible without needing hover.

**Focused by mouse click.**
Clicking anywhere in the resting bar expands it in place: it grows
slightly taller and gains a results dropdown anchored directly beneath it,
still positioned inline in the sidebar column (not centered on screen, not
enlarged in font size). The click places a normal text caret; existing
text is not auto-selected.

**Focused by keyboard (e.g. a "focus the address bar" shortcut).**
This is a materially different presentation: the bar detaches from the
sidebar row and appears as a floating panel centered on the screen — both
horizontally (dead-center) and vertically (centered against the window,
biased so the results list below it has consistent room). It becomes
noticeably larger: taller, bigger type, rounder corners, a light
background (optionally with a soft blur if translucency is enabled), and
a pronounced soft drop shadow that lifts it visually off the page content
behind it, plus a hairline border. All existing text is selected
(highlighted end-to-end), ready to be replaced by typing — exactly like a
typical "jump to address bar" shortcut elsewhere. It sits above every
other UI surface in the window.

The two behaviours (float on keyboard-focus vs. stay inline on
click-focus) are independently controlled by one setting, defaulting to
"float only when not click-focused" — the other available modes are
"always float on any focus" and "never float."

**Losing focus (blur).**
If the user typed something but never navigated and then clicks/tabs away,
the bar's default behaviour is to snap back to displaying the current
page's URL (so it never idles showing a half-typed query) — this reversion
is specific to the compact single-row layout; an alternate, classic
two-row layout keeps whatever was typed instead. Both layouts are
available as a user setting; the compact one is the default.

**Results dropdown while typing/focused.**
Each result row shows a favicon, title, URL, and (for switch-to-tab
results) a right-aligned pill plus a directional arrow glyph. The
alternate-search-engine icon row beneath the input, which stock builds
show, does not appear at all unless explicitly re-enabled.

### Extensions

**Two homes for extension icons: pinned (inline) and everything else (a panel).**
An extension a user has pinned shows as a small square icon that lives
directly beside the address bar's built-in icons (near its trailing/inner
edge), not in a separate always-visible toolbar strip. Extensions the user
hasn't pinned don't appear anywhere in the bar at rest; they're reached
through a single button (see "Consolidated actions button") that opens a
panel listing every installed extension, each with a "pin to bar" /
"unpin" toggle — pinning it adds it to the inline set described above;
unpinning removes it back into the panel-only list. This pin/unpin toggle
lives inside that same list panel (not as separate drag-and-drop toolbar
customization), applied per item.

**Hover reveal.**
At rest, pinned extension icons (along with a couple of other small
per-page icons that live in the same row) are present in the layout but
invisible — zero opacity, not interactable by click, though they don't
take up phantom clickable space beyond their own already-open state. They
fade in over roughly 150ms as soon as the pointer enters the broader top
toolbar area that contains the address bar (not narrowly just the input
field itself — the whole row, including the space around it) or when: one
of those icons already has its own popup open, the address bar itself has
an open dropdown, or the current tab is a blank/new tab (in which case
they're shown outright, no hover needed). There's a brief internal
debounce (effectively near-zero by default) to avoid flicker from mouse
jitter crossing the row boundary, but no separate "wait before showing"
delay beyond that — showing is essentially immediate on hover entry, and
fades back out over the same ~150ms once the pointer leaves (again with a
short settle-time guard against accidental flicker).

**Overflow.**
If there isn't room to fit every pinned extension icon inline next to the
address bar, the excess ones move into a compact grid of icons in a
separate strip directly above the tab list, arranged as auto-sizing
squares (roughly 32px minimum each) with small gaps. Unlike the inline
set, this overflow strip is not hover-gated: once it has any icons in it,
it is simply visible, with no fade or hover requirement (though it stays
absent/collapsed when empty).

**Consolidated actions button.**
By default, the classic separate "manage extensions" toolbar button
(commonly a puzzle-piece icon, kept apart from the URL bar in most
Chromium builds) is removed from the toolbar and its job is folded into
one existing icon already living in the address bar (the site/security
info icon area). Clicking that one icon opens a single panel that
combines: page-level actions (bookmark, share, security/permissions info,
site data), and the extensions list with its pin/unpin toggles described
above. This is a setting the user can flip back to restore the separate
button if they want it distinct.

---

**Source root for all citations above:** https://github.com/zen-browser/desktop
(branch `dev`). Key files, for quick reference:
- `src/zen/common/styles/zen-omnibox.css`
- `src/browser/components/urlbar/content/UrlbarInput-mjs.patch`
- `src/zen/common/modules/ZenUIManager.mjs`
- `src/zen/compact-mode/ZenCompactMode.mjs`
- `src/zen/urlbar/ZenSiteDataPanel.sys.mjs`
- `src/zen/common/styles/zen-overflowing-addons.css`
- `src/browser/base/content/browser-addons-js.patch`
- `src/browser/base/content/navigator-toolbox-inc-xhtml.patch`
- `prefs/zen/zen-urlbar.yaml`, `prefs/zen/view.yaml`, `prefs/zen/theme.yaml`
- `src/zen/tests/urlbar/browser_floating_urlbar.js`
- `src/zen/tests/urlbar/browser_single_toolbar_blur_revert.js`
- `src/zen/tests/site_control/browser_site_control_shows_installed_addons.js`
