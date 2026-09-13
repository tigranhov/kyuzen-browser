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

## Showing which storage a space uses

Checked 2026-09-13, after the owner read a deliberately shared space as a leak
during the hand pass. Two of the three seeded spaces share the default logins
on purpose, and nothing on screen announces that.

**Zen marks the tab, not the workspace.** A workspace bound to a container
gives its tabs a container indicator, which Zen shows more prominently than
Firefox does — there it is a coloured stripe along the edge of the tab. The
telling detail is a setting named **"Hide default container indicator"**: the
case worth marking is a jar that is *not* the default one, and users who find
even that mark noisy switch it off. Zen's own bug reports about indicators
appearing wrongly take that setting as their baseline
(zen-browser/desktop#1792 and #8470).

**Zen does not show which workspaces share a jar.** Its workspace
documentation does not say, and the community answer is a workaround rather
than a feature: colour each workspace by hand to match its container, which a
maintainer notes is already possible because containers have colours and
workspaces have their own themes (zen-browser/desktop discussion #2630). A
user in the same thread asks for the whole interface to turn red on a
production container — the same wish approached from the other end, and also
not a thing the browser does.

Arcium already sits where Zen sits. The space bar draws a small disc filled
with the current space's profile colour, carrying `Profile: <name>` as its
tooltip and accessible name, with the profile menu hanging off it; the shared
default keeps preset 0, the accent the badge drew before profiles existed, so
a badge that looks unchanged means shared logins. That is Zen's shape exactly:
the non-default jar is what gets marked.

**Ruling: no new sidebar UI in this stage.** The confusion during the pass came
from a harness that signed tabs back in on every reload, not from a missing
badge, and the badge already answers "whose logins is this space using". The
question neither browser answers at a glance is "which *other* spaces share
this one's logins", and deciding that belongs with the customisation work,
where space gradients and profile colours are edited side by side and a user
can make the answer obvious themselves the way Zen's users do. If this is
wrong, the cost is a later addition to a view that already holds the data.

## What this changed

Nothing in the code, on either visit. All three rulings stand, and the entries
in `docs/stage3b-findings.md` cite this note instead of resting on Arcium's own
reasoning alone.
