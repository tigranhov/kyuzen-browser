# Stage 4a design: the address pill, the command box and extensions

Status: approved in the design session of 2026-09-13. Brought forward from
Stage 4 at the owner's request, ahead of the rest of that stage, because the
browser is close to usable every day and two things stop it: an extension can
be installed but has nowhere to appear, and the address pill still wears every
icon Chromium's own bar carries. Implements R4.1, R4.2 and R4.8 of the master
spec, and a revised R4.7. Little Arc, Peek, Air Traffic Control and site search
shortcuts stay in Stage 4 and are not touched here.

Research this design rests on:

- `docs/research/zen-url-bar-and-extensions.md` (2026-09-12): Zen's URL bar and
  extension placement, read from `zen-browser/desktop` source.
- A second reading on 2026-09-13 of Zen's own stylesheets, its site-data panel
  and its published screenshots, to see the composition rather than the rules.
- The owner's account of using Zen daily. Where the source and the owner
  disagreed, the owner wins: the source shows a puzzle button in the top row,
  the owner reports the entry point is a button inside the bar revealed on
  hover, and that is what this builds.

---

## 1. Goal

The address pill shows the domain and nothing else. Hovering it reveals two
buttons at its trailing edge, one opening extensions and one copying the link.
Clicking it opens one floating box, the same box Cmd+T opens, and that box
answers as you type. Extensions you pin appear as a row of small buttons above
the favourites, always visible, and clicking one opens its popup.

The measure of done is that the owner can use Arcium as their only browser with
iCloud Passwords and the Claude extension installed.

## 2. Decisions

Made by the owner in the design session:

- **D1.** All four parts ship together. A pill without suggestions is not
  something to live in.
- **D2.** Bookmarking is dropped entirely, not deferred. Spaces and pinned tabs
  already carry the idea, and a second one competing with them is worse than
  none. Nothing in this stage bookmarks, and Cmd+D is not wired.
- **D3.** At rest the pill shows the domain alone: `google.com`, not
  `https://www.google.com/`.
- **D4.** Hovering reveals exactly two buttons at the trailing edge: extensions
  and copy link.
- **D5.** One box serves both Cmd+T and a click on the pill, over the page, and
  it offers suggestions as you type.
- **D6.** Pinned extensions are a row of small buttons directly above the
  favourites, always visible, smaller than a favourite tile.

Decided here, by following Zen where Zen has an answer:

- **D7.** Nothing about security shows at rest, with one exception in D8: Zen
  keeps the whole identity area collapsed until the pointer arrives.
- **D8.** A connection that is **not** secure shows a small mark in the pill
  without hovering. Zen does not do this. Hiding every other affordance costs
  the user a click; hiding this one costs them the warning itself, which is the
  only thing in the bar whose whole value is being seen unasked.
- **D9.** Security and site permissions are reached through Chromium's own page
  information bubble, opened by a site button at the pill's leading edge, and
  extensions through Chromium's own extensions menu, opened by the pill's
  extensions button. The site button is the not-secure mark when a connection
  is not secure, and so is always there when it matters; otherwise it appears
  on hover, exactly as Zen's own identity area does. Zen merges security,
  permissions and extensions into one panel of its own. We do not, because
  merging means rebuilding page information -- a security-sensitive surface
  Chromium already ships, maintains and localises -- for no gain the user can
  see. Two entry points instead of one is the whole difference.

## 3. The address bar stays, and stops drawing

Arcium already moves Chromium's `LocationBarView` bodily into the pill. It
stays there.

It has to. Chromium's password bubbles, its permission prompts and its page
information bubble all find their anchor through
`ToolbarView::GetBubbleAnchor`, which returns the location bar and falls back
to the top container when that bar is not drawn -- and Arcium's top container
is the wrong end of the window. Worse, a permission prompt is not merely
anchored to the bar: it *is* a chip owned by `LocationBarView`, and
`PermissionPromptChip` refuses to show at all when the bar is not in a valid
state. Removing the bar would cost the save-password bubble this stage exists
to make work, and every camera and microphone prompt besides.

So the bar stays where it is, at the pill's bounds, and stops showing anything
of its own: no background, no border, no location icon, no page actions, no
omnibox text. One thing it keeps is the permission chip, which appears inside
the pill when a site asks for something -- exactly where a user expects the
question, and exactly what Zen does, whose icons are collapsed at rest but
visible the moment they have something to say.

The pill draws over it: its own quiet fill, its own domain text, its own two
buttons.

## 4. The pill

`UrlPillView` grows from a placeholder host into the surface itself.

**At rest.** A quiet fill, no border, no shadow. One line of text: the
registrable domain of the active tab's URL, elided at the trailing edge. A page
with no host -- a new tab, a settings page -- shows a short label instead of an
empty pill. If the connection is not secure, the site button shows as a warning
mark at the leading edge, without hovering (D8).

**On hover.** Two buttons fade in at the trailing edge over 150ms, the timing
Zen's stylesheet uses, and fade out on leave. The first opens the extensions
menu, the second copies the current URL and briefly says so. On a connection
that is secure, the site button fades in at the leading edge on the same terms.
The text area shrinks to make room rather than the buttons overlapping it. The
row is focusable, and keyboard focus reveals all three on the same terms as
hover, because a control reachable only by pointer is not reachable.

**On click.** Anywhere that is not one of those three buttons -- the domain text
included -- opens the command box (section 6). The hosted address bar is never
focused, so Chromium's own dropdown never appears; there is exactly one place
suggestions come from.

**The site button** opens Chromium's page information bubble through
`ShowPageInfoDialog`, anchored at the pill, which is where connection security
and this site's permissions live (D9).

## 5. Extensions

Chromium builds the whole machine already: `ExtensionsToolbarDesktop` holds one
button per pinned extension plus a menu button, `ExtensionsMenuCoordinator`
opens the list with its pin and unpin switches, and every part of it is Views,
so nothing here adds a process or a WebUI surface. The work is placement.

**The row.** The container is reparented out of the hidden toolbar into a new
row between the pill and the favourites, the same move the location bar already
makes, so the browser's `ExtensionsContainer` pointer stays valid and extension
popups keep finding their owner. The row wraps: buttons are 26px on a 32px
pitch, `kSidebarPadding` in from each edge, and a second line starts when a
line is full. Empty, the row takes no height at all and no space in the layout.

**No menu button in the row.** The container is constructed in
`DisplayMode::kAutoHide`, which exists for exactly this -- windows that want as
few visible icons as possible -- and keeps the menu button out of the strip
while leaving it callable. The pill's extensions button calls
`ExtensionsToolbarButton::ToggleExtensionsMenu()` on it.

**Pinning** is Chromium's, untouched: the menu's own switches move an extension
between the row and the list. There is no drag-to-customise.

**Popups** anchor to the button that opened them, in the sidebar, as they do in
the toolbar today.

## 6. The command box

`QuickEntryBubble` today is a bare text field. It becomes the one place
addresses are typed, and it answers.

**Suggestions.** A new `SuggestionSource` in `arcium/ui/browser` owns an
`AutocompleteController` built on `ChromeAutocompleteProviderClient`, the same
one Chrome's own bar and the new tab page's search box use, with
`unscoped_open_tab_suggestions` on so open tabs are offered without having to
ask for them by keyword. It observes results and hands the box rows: an icon, a
title, a URL, and what kind of thing each is. Suggestions come from history,
open tabs across every space, and the search engine. Bookmarks are not a source
here, because D2 means there are none.

**The list.** Rows under the field, the first one selected. Up and down move,
Enter opens, Escape closes and leaves the page alone. A row that is an open tab
says so and switches to that tab instead of loading it a second time, switching
space first if the tab lives in another one. Everything else navigates in the
active space.

**Where it opens.** Over the page, as now. The same box for Cmd+T and for a
click on the pill; Cmd+L opens it too, with the current address in it, selected.

**Nothing runs while it is closed.** The controller is created with the box and
destroyed with it, so a browser sitting idle carries no autocomplete providers,
no timers and no network work from this feature.

## 7. Patches

Three seams, all hooks, no logic:

- `LocationBarView`: paint nothing, lay out none of its own children, keep the
  permission chip. Delegates to a decision function in `arcium/ui/browser`.
- `ToolbarView::Init`, where the extensions container is constructed: use
  `kAutoHide` when Arcium owns the window. Delegates to `arcium`.
- `BrowserCommandController`, the existing Cmd+T patch, extended to take
  `IDC_FOCUS_LOCATION` as well so Cmd+L opens the same box. The header is
  updated to name both commands.

## 8. Testing

Unit tests, in `arcium/test/`:

- The domain shown for a URL: host with `www.` dropped, a port kept, an IP
  address, a file URL, a page with no host, a very long host elided.
- Whether a URL counts as not secure, so the mark appears for `http`, a bad
  certificate, and mixed content, and not for `https` or an internal page.
- The suggestion source: rows come out in the controller's order, an open-tab
  row is marked as one, and the source starts nothing until asked.

Browser tests, in `arcium/test/browser/`:

- A pinned extension draws a button in the sidebar row, and clicking it opens
  that extension's popup.
- Unpinning removes the button and the row collapses to nothing when the last
  one goes.
- The pill's hosted address bar draws no children of its own, while a permission
  request still shows its chip there.
- Hovering the pill reveals its buttons; keyboard focus reveals them too.
- The site button opens page information, and shows as a warning without hover
  on an insecure page.
- Clicking the pill opens the box, and the address bar does not take focus.
- Typing in the box produces rows, and Enter on an open-tab row activates that
  tab rather than opening a second one -- with a positive control, since an
  assertion that no tab was added is also what a broken test reports: opening a
  row that is not an open tab must add one.
- Cmd+L opens the box with the current address selected.

Each is written before its code and watched to fail.

## 9. Acceptance

- **A4a.1** Install iCloud Passwords and the Claude extension. Both appear in
  the row above the favourites once pinned, both open their own surface when
  clicked, and both survive a relaunch.
- **A4a.2** Save a password on a site. Chrome's save bubble appears at the
  pill, not at the top of the window.
- **A4a.3** A site asks for the microphone. The request appears inside the pill
  and answering it works.
- **A4a.4** The pill reads `google.com` on Google. Hovering reveals two
  buttons; copy puts the full address on the clipboard.
- **A4a.5** Open an `http` page: the not-secure mark shows without hovering,
  and clicking it opens page information saying the connection is not secure.
- **A4a.6** Cmd+T, type three letters of a site in history: it is offered, and
  Enter goes there. Type the title of a tab open in another space: the row says
  it is open, and Enter switches to it rather than loading it again.
- **A4a.7** Pin nine extensions on a 250px sidebar: seven fit on a line, the
  row wraps to a second, and none are clipped.

## 10. Performance

1. **A process, or one kept alive longer?** No. Every surface here is Views:
   the extensions menu, the page information bubble and the command box. The
   autocomplete controller lives only while the box is open.
2. **Idle memory?** Per window, the pill's own views and an empty extensions
   row, which is nothing measurable. Per tab, nothing. The extensions container
   already exists in every window today; this moves it.
3. **Work before first paint?** None added: the row and the pill are built with
   the sidebar, and no suggestion machinery is constructed at startup.
4. **UI-thread work not needed for the frame?** The autocomplete providers do
   their work off the UI thread already and answer on it, as they do in Chrome.
   The domain string is computed when the active tab's URL changes, not per
   frame. No sync I/O.

Measured with `scripts/perf` at the end of the stage.

## 11. Out of scope

- Bookmarking, permanently (D2).
- Little Arc, Peek, Air Traffic Control, site search shortcuts: the rest of
  Stage 4.
- The floating, enlarged focused bar Zen shows on keyboard focus. Our box is
  one size and one position.
- Reader mode, zoom and picture-in-picture controls. Reachable from menus; no
  home in the pill.
- Commands in the box. R4.1 wants it to run commands as well as open pages;
  this builds the opening half, and commands wait for the rest of Stage 4.
- A settings surface for any of this. Nothing here is optional yet.

## 12. Master spec corrections

R4.7 and R4.8 are written against a reading of Zen's source that this design
supersedes in three places, and the master spec should be amended to match:

- R4.8 says Chromium's puzzle-piece button is removed and the extensions list
  lives in a consolidated page-actions button. The list does open from a button
  in the pill, but there is no consolidated page-actions button: the second
  button copies the link, and page information opens from the domain text.
- R4.8 places pinned extensions inline beside the pill with an overflow strip
  below it. They are one row above the favourites, which wraps, and there is no
  separate overflow surface.
- R4.7's consolidated button -- reader mode, bookmark, page-action overflow and
  the go arrow behind one icon -- does not exist. Bookmarking is gone entirely,
  and the rest is out of scope.
