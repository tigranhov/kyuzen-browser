# How Zen isolates logins per workspace, and what it does not promise

Checked 2026-09-12, while settling two open questions in the profiles stage.
Recorded because the answers changed nothing but confirmed two rulings, and a
future reader deserves to know the rulings were checked against Zen rather
than argued from Arcium's own logic alone.

## What Zen does

Zen's workspaces are paired with **Firefox container tabs**, not with separate
browser profiles. A container is a separate cookie jar inside one profile; a
workspace can name a container as its default, and tabs opened in that
workspace then inherit that jar. A work Google sign-in in one workspace and a
personal one in another therefore coexist, which is the same user-visible
promise Arcium makes with storage partitions.

Two limits are stated plainly in Zen's own documentation, and both matter
here:

- **Containers separate cookies and session state. They do not separate
  history, and they do not separate extensions.** Arcium's design makes the
  same split deliberately, and two of this stage's acceptance checks assert
  it: an extension stays installed across profiles, and its options page
  shows the same saved settings opened from any space.
- The binding is described in terms of **tabs opened in the workspace** —
  what a new tab inherits. This is the same shape as Arcium's rule that a tab
  cannot change its storage, so moving one between profiles is a reopen.

## What Zen does not say

**Nothing in Zen's workspace documentation defines what happens to a tab that
is already open when a workspace's container binding changes**, and nothing
addresses a tab of that workspace being open in a second window. The silence
is the finding: the stronger guarantee Arcium was weighing is one the
reference browser does not make either.

There is a long-standing Firefox request to change the container of an
already-open tab in place (mozilla/multi-account-containers#326). **Its
existence is all that was confirmed** — the maintainers' reasoning was not
visible when this was checked, so nothing here should be read as "Firefox
cannot do it". It is weak evidence, recorded as weak evidence.

## The redirect case, which Firefox got wrong for a while

Firefox's container extension carried a confirmed bug of exactly the shape
Arcium's reopen mark exists to bound: a link opened in one container that then
redirected to a page assigned to a **different** container would sometimes
open that page **twice**, intermittently
(mozilla/multi-account-containers#940, later closed by a fix). Re-deciding a
page's cookie jar mid-redirect is, empirically, where duplicate tabs come
from.

Arcium's guard spends its relocation mark on the first navigation it sees, so
a relocated load's own redirects go unchecked. That is the deliberate trade
recorded in the findings: the mark is what stops a defect in a creation hook
from reopening a tab forever, and the browser this project follows has already
shipped the duplicate-tab failure that the other choice invites.

## What this changed

Nothing in the code. Both rulings stand, and both entries in
`docs/stage3b-findings.md` now cite this note instead of resting on Arcium's
own reasoning alone.
