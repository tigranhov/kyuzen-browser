# Network audit findings

Each row: a host contacted at idle with a fresh profile that is not on `docs/netaudit-allowlist.txt`,
the Chromium subsystem that owns it, and the decision. Re-run `scripts/netaudit 60` after each stage.

## 2026-09-06, Stage 0 baseline (Chromium 152.0.7977.83, zero patches)

| Host | Owner | Decision |
|---|---|---|
| `android.clients.google.com` | GCM engine check-in (`google_apis/gcm/engine/gservices_settings.cc`), the transport for Web Push notifications and Chromium's invalidation service | Product decision for Stage 1: keep GCM so sites can send push notifications, at the cost of one background connection, or disable it (`gcm` feature / pref default) for a lighter idle. Leaning keep, off until a site is granted notification permission if Chromium's driver allows lazy start |
| `accounts.google.com` | Signin infrastructure (`components/signin`, `google_apis/gaia`), likely the Gaia cookie or token-binding checks that run regardless of API keys | Stage 1: pref default `signin.allowed = false` via a hook, then re-audit. Should also remove the visible sign-in controls in Settings |
| `csp.withgoogle.com` | CSP violation reporting triggered by the `accounts.google.com` response | Disappears with the row above; no separate action |
| `www.google.com` | Default search engine: Google domain check and suggest/prefetch (`TemplateURLService`, omnibox) | Product decision on default search engine, Stage 1 or the Stage 4 command bar. Any engine will contact its host at idle for suggestions |

Allowed and seen: `clients2.google.com` (component updater; downloaded Widevine and CRL sets).
