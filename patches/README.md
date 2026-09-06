# Patches

Ordered series applied to the Chromium checkout by `scripts/sync`.

Naming: `NNNN-short-name.patch`, four-digit, gaps of 10 to allow insertion.

Every patch header states: what upstream seam it hooks, why the hook is needed, and which
`arcium/` code it delegates to. A patch contains no logic, only the hook.

## Status

Stage 0 shipped with zero patches. Branding is selected by GN (`branding_path_component`), and
Google services are removed by GN args in `build/common.gni`. Keep it that way as long as possible.

## Monthly rebase routine

1. Find the new stable tag on https://chromiumdash.appspot.com/releases?platform=Mac
2. `scripts/rebase <tag>`; fix any conflicting patch by moving its hook to a more stable seam.
3. `scripts/build dev`, run the last completed stage's acceptance list by hand.
4. `scripts/netaudit 60`, then `scripts/perf --label rebase-<tag>` when the machine is quiet.
5. Commit `CHROMIUM_VERSION` and any patch changes together with a message naming the tag.
