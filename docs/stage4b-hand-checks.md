# What to test by hand: peek, outside links, routing and box commands

Everything below is built into `out/dev` and passes its automated tests. What
no test can produce is a real click, a link handed over by another
application, and a judgement about whether a thing looks right.

## What has already been run

**2026-09-16.** Twenty rows were driven from this machine's own keyboard and
pointer against a real browser: 1, 2, 11 to 14, 16 to 25, and 27 to 30. All
passed. Three things wrong that no row asked about were found in the same
sitting and are fixed, and each is worth a look while walking the rest:

- The blank tab a window lands on when it switches to a space with nothing
  open read "Untitled". It now reads "New tab".
- The pill was empty on a local file and on a settings page. It now names the
  file, and says `chrome://settings` for a page belonging to the browser.
- Launching the browser while it was already running opened a second window.
  It now raises the window that exists, and an address on that command line
  opens as a tab in it.

**2026-09-20.** The peek rows 3 to 9, the close-a-tab row 26 and the site
search rows 31 to 34 were driven the same way, and all passed. The peek rows
were walked twice, because the first walk found the card drawing with no
dimming and no buttons: the second walk, after that was fixed, saw the page
behind go grey and pressed the open-as-tab button by eye.

One thing turned up in row 26 that no row asked about, and it is fixed: the
box's rows answered the keyboard only, so the highlight did not follow the
pointer and a click on a row did nothing. They now mark themselves under the
pointer and open on a click, with the row Enter would take keeping its own
stronger mark.

What is left is rows 10 and 15, plus the seven Stage 4a rows in
`scripts/acceptance-4a`. Both remaining rows need something no agent on this
machine can produce: another application handing over a link, and a judgement
about which window a keystroke should close.

Start the browser with your own profile:

```
scripts/run
```

Each row says what to do and what should happen. Write the result beside it,
and say "wrong" for anything that looks off even where the rule below says it
is intended.

## 1. The page fills the window (R1.6)

1. Look at the edge between the sidebar and the page. There should be no gap
   and no margin anywhere, and the page's corners should be rounded.
2. Hide the sidebar and show it again. The page should keep its corners and
   still touch the sidebar.

## 2. Peek: a link that leaves a pinned tab's home (A4b.1)

3. Pin a tab on a site with outgoing links, for example a news front page.
4. Click a link that goes to another site. It should open **over** the tab as
   a card with the page behind it dimmed, and the pinned tab should stay where
   it was.
5. Press Escape. The card should close and the pinned tab should be unchanged.
6. Do it again and press the second button beside the card, the one for
   opening it as a tab. The page should become an ordinary tab in the same
   space, at the end of the list, and the card should be gone.
7. Do it again and click the dimmed area outside the card. It should close.
8. Do it again and switch to another tab. The card should put itself away.
9. Click a link that stays on the same site. No card: the tab should navigate
   as usual.

## 3. A link from another application (A4b.2)

10. Make Arcium your default browser, then click a link in Mail, Slack,
    Messages or Notes.
11. A small window, about 480 by 640, should open near the top right of the
    screen, with the site's name and a button offering to open it in a space.
    The main window should not gain a tab.
12. Press that button. The page should move into the main window as a tab in
    the named space, that space should come to the front, and the small window
    should close.
13. Click two links in a row. Two windows should open, the second stepped down
    and to the left of the first.
14. Open a small window and close it with the red button. The page should go
    with it, and the main window should still have no extra tab.
15. **The open question:** open a small window, put the keyboard on it and
    press Cmd+W. Say which window closes and whether anything in the main
    window closes with it.
16. Open a file from Finder with Arcium, for example a PDF or an HTML file. It
    should open the ordinary way, in a tab, not in a small window.

## 4. A site that always opens in one space (A4b.3)

17. Make a second space if you do not have one.
18. In the second space, right-click a row on github.com and choose "Always
    open github.com in this space".
19. Right-click that row again. It should now offer to stop, not to start.
20. Go back to the first space and type a GitHub address into the box. It
    should open in the second space, and the window should switch there.
21. Click a GitHub link in another application. The small window should say
    the second space's name, and its button should put the page there.
22. Type a GitHub address in the second space itself. It should just open
    there, with no switching.
23. Choose "Stop opening github.com in this space" and repeat step 20. It
    should now open where you are.
24. Quit and start the browser again, then repeat step 20 with the rule in
    place. The rule should have survived.

## 5. Commands in the box (A4b.4)

25. Open the box and type "clo". It should offer closing a tab, reopening a
    closed one, and duplicating a tab, above any web suggestions.
26. Choose Close tab. The tab on screen should close.
27. Type "dev" and choose Developer tools. They should open.
28. Type "new sp" and choose New space. A space should appear in the bar.
29. Type "hide" and choose the sidebar command. The sidebar should hide, and
    the same command should bring it back.
30. Type an ordinary address. No command rows should appear, and Enter should
    open the page as before.

## 6. Searching one site from the box (A4b.5)

31. Type "site search" in the box and choose the shortcuts command. Settings
    should open on the search engine list.
32. Give YouTube the keyword "yt" there.
33. Back in the box, type "yt cats". A row should read "Search YouTube for
    cats" rather than just "cats".
34. Press Enter. YouTube's own search results should open.

## What is deliberately not here

A peek opened by holding a key while clicking: the design left that out, so
only a link that leaves a pinned or favourite tab's home opens one.
