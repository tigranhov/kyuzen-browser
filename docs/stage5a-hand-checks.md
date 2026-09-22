# Stage 5a hand checks (A5.1)

Ten rows, walked against a running browser with this machine's own keyboard
and pointer. Each says what to do and what to look for; none of them names a
function, because a row an agent can satisfy by reading code is a row that
proves nothing about what the reader sees.

Launch with `scripts/run`. Rows 7 and 10 assume a space other than the one on
screen, so make a second space first and put a page in it.

| # | Do this | Look for |
|---|---|---|
| 1 | Drag a Today row onto the right half of the page | The right half lights up under the pointer while you hold it there, and on release two pages are on screen, the dragged one on the right |
| 2 | Scroll each pane, then navigate the left one | Each scrolls on its own, and navigating the left leaves the right exactly where it was |
| 3 | Drag the divider left and right, as far as it will go each way | Both panes resize as you drag, and neither disappears: the divider stops short of each edge |
| 4 | Look at the sidebar with the split up | Both rows are marked current, and where the two rows are neighbours a bracket joins them down their leading edge; where they are not neighbours each carries the two-pane mark instead |
| 5 | End the split, then split a pinned entry with a Today tab from the pinned row's menu | Two pages again, and both rows carry the mark — the pinned one and the Today one, in their own sections |
| 6 | Switch to another space and back | The split is still there, both pages still loaded, and the divider is where you left it |
| 7 | With a split up in a space you are **not** showing, quit and relaunch | The split is back. The space you were in when you quit is the one on screen — the window did not follow the split into the other space. Nothing in the other space has loaded until you enter it |
| 8 | Move one half to another space, from its row menu | The split ends the moment the move happens, both tabs survive, and the moved one is in the other space with its history intact |
| 9 | Press Cmd+Option+S with no split up, then again | The first forms a split with the page you were on before this one; the second breaks it and leaves both tabs open |
| 10 | Open the command box, type "split", take the command, then type part of another tab's title and take it | After the command the box lists tabs you could split with, and taking one puts it beside the page you were on |

A row that fails is a finding: write what you saw, not what it should have
done, and say which of the ten it was.
