# Perf: stage0-baseline (2026-09-06)

Config `dev`, Chromium `152.0.7977.83`, arcium `fbafc5f`, 3 runs, 60 s idle, fresh profile, about:blank.
Method: see scripts/perf. Numbers are medians.

| Metric | Median |
|---|---|
| Startup to DevTools endpoint (ms) | 2630 |
| Idle RSS, all processes (MB) | 1490 |
| Process count | 9 |

Raw runs: `[{"startup_ms": 2572, "idle_rss_mb": 1505, "process_count": 9}, {"startup_ms": 3308, "idle_rss_mb": 1472, "process_count": 9}, {"startup_ms": 2630, "idle_rss_mb": 1490, "process_count": 9}]`

## Caveats

- Dev config: component build, DCHECKs on, ~250 dylibs loaded from a USB SSD. Startup is dominated
  by dynamic loading and is not representative of a user build; the `perf` config is for that.
- Idle RSS sums per-process resident sets, so pages shared between processes are counted once per
  process. Use it only relative to other runs of the same method.
- The machine was under load from other work during these runs (see load notes in the session).

## Smoke checklist (spec A0.1), executed 2026-09-06

- [x] Window opens, pages render, tabs open and close
- [x] H.264 MP4 plays; MediaSource reports H.264, AAC, HEVC, AV1 supported
- [x] `chrome://version`: executable under `Arcium.app`, profile under `Application Support/Arcium-dev`
- [x] DevTools opens (fn+F12, confirmed by hand)
- [x] Chrome Web Store installs uBlock Origin Lite (confirmed by hand; store shows a "Switch to Chrome?" banner, see carry-over)
- [x] Sync disabled; sign-in cannot complete without keys (controls still visible, see carry-over)
- [x] "Google API keys are missing" infobar present, as expected for this stage
- [x] Widevine CDM downloaded by the component updater; `requestMediaKeySystemAccess` granted on an https origin
