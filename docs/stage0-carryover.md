# Stage 0 carry-over: findings for Stage 1

Observed on the first Arcium build (Chromium 152.0.7977.83, 2026-09-06). None blocks Stage 0's
acceptance; each is an input to the Stage 1 plan.

| Finding | Where seen | Proposed handling |
|---|---|---|
| Product strings still say "Chromium" ("Your Chromium", `chrome://version`, "Sign in to Chromium") | every WebUI page | Strings come from `branding_path_product`, which selects `chrome/app/${product}_strings.grd` and `components/components_${product}_strings.grd`. Provide `arcium` variants of both (copies of the chromium ones with the name replaced) via symlinks like the theme dirs, then set `branding_path_product = "arcium"`. No patch expected |
| "Google API keys are missing" infobar on every window | first window | One-line hook patch at the infobar creation site (`chrome/browser/ui/startup/`), delegating the decision to `arcium/`. First patch of the project |
| Sign-in controls visible in Settings > You and Google; they cannot work without OAuth keys | `chrome://settings/people` | Hide the section via a pref default or a hook; Sync already reports "disabled by your administrator" |
| Web Store shows a "Switch to Chrome?" banner with an "Install Chrome" button, though "Add to Chrome" works | Web Store detail page | The store reads the user-agent client-hints brand list, which says "Chromium" not "Google Chrome". Decide whether to present a Chrome brand in client hints (Brave does). Install itself is confirmed working |
| Default search engine and `www.google.com` contact at idle, if the net audit shows it | `scripts/netaudit` | Search-engine choice is a product decision for Stage 1 or 4 (command bar) |
| Two `objc` "class implemented in both" warnings at launch (ANGLESwapCGLLayer) | stderr | Component-build artefact, harmless, disappears in non-component builds |

## Verified working, no action

Extensions install from the Web Store; DevTools; H.264, AAC, HEVC, AV1 and MP4 playback;
Widevine via the component updater; Sync disabled by construction; app bundle branding and icon.
