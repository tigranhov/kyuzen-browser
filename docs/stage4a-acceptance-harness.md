# The Stage 4a acceptance harness

`scripts/acceptance-4a` builds the world the seven Stage 4a acceptance rows
assume, and prints them as a checklist. It does not run the pass: most of these
rows need a person, because they are about installing the owner's own
extensions, answering a permission prompt, and reading what a bubble says.

## What it sets up

A throwaway profile under `Arcium-acceptance-4a`, so the pass never touches the
browser the owner uses, and a one-page site on loopback carrying three things
the rows need:

- a **plain `http` connection**, for the not-secure mark and page information;
- a **sign-in form that posts nowhere**, so Chrome offers to save a password
  and the offer's position can be read;
- a **button that asks for the microphone**, so a permission request appears.

`--keep` reuses the profile instead of starting clean, which is what the
"survives a relaunch" halves need.

## The one substitution

A4a.5 asks for a site whose connection is not secure. It gets a page served
over plain `http` on loopback rather than a real insecure site on the internet.
That is a substitution of the site and not of the property being checked:
"not secure" is a statement about the connection, and loopback `http` is
exactly such a connection, judged by the same `SecurityStateTabHelper` that
judges any other page. Everything else in the pass is the real thing.

## What it deliberately will not do

It does not install iCloud Passwords or the Claude extension. Those are the
owner's accounts and the owner's clicks, and a script that signed into a store
to fetch them would be doing something this project has no business doing. It
prints the two pages to open instead.

## No real credential

The sign-in form exists so Chrome offers to save something. **Any made-up name
and password will do, and a real one must never be typed into it.** The row is
about where the bubble appears, not about an account. This is the same rule the
Stage 3b harness follows, for the same reason.
