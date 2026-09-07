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

## 2026-09-06, Stage 1 (sidebar, sign-in disabled, patches 0010 to 0100)

Same four hosts as the baseline, so disabling sign-in did not stop the sign-in traffic.

| Host | Change since Stage 0 | Decision |
|---|---|---|
| `android.clients.google.com` | unchanged | GCM check-in, kept on purpose so sites can use Web Push. Revisit in Stage 7 when idle cost is measured |
| `accounts.google.com` | still contacted although `signin.allowed` is false | The pref hides the UI but does not stop the Gaia cookie check at startup. Stage 3 owns profiles and sign-in; disable the fetcher there rather than guess now |
| `csp.withgoogle.com` | unchanged | Follows the `accounts.google.com` response; goes away with the row above |
| `www.google.com` | unchanged | Default search engine, as decided in Stage 0 |

Allowed and seen: `clients2.google.com` (component updater).

## 2026-09-07, Stage 2 (Arc tab model, patches 0010 to 0140)

`scripts/netaudit 120`, fresh profile, `about:blank`, config `dev`, arcium `92523d6`. The allowlist
is **unchanged** — nothing Stage 2 added talks to the network at all. The live model is a local
JSON file, the archive is a local SQLite database, tab search is an in-memory index over both, and
none of the three has a fetcher. Same four unexpected hosts as Stage 0 and Stage 1, with the same
owners and the same open decisions; no new host appeared and none went away.

```
[arcium] running 120 s with fresh profile; net-log at /tmp/arcium-netaudit.ihjDl8/netlog.json
contacted hosts:
   ! accounts.google.com
   ! android.clients.google.com
     clients2.google.com
   ! csp.withgoogle.com
     edgedl.me.gvt1.com
     update.googleapis.com
   ! www.google.com
unexpected hosts: 4
```

Exit code 1, as at Stage 0 and Stage 1: the script fails on any unexpected host, and these four
are carried deliberately rather than fixed. They are not a Stage 2 regression.

| Host | Change since Stage 1 | Decision |
|---|---|---|
| `android.clients.google.com` | unchanged | GCM check-in, still kept on purpose for Web Push. Stage 7 measures the idle cost |
| `accounts.google.com` | unchanged; `signin.allowed` is still false and still does not stop it | Stage 3 owns profiles and sign-in and disables the fetcher there |
| `csp.withgoogle.com` | unchanged | Follows the `accounts.google.com` response; goes away with it |
| `www.google.com` | unchanged | Default search engine, as decided in Stage 0 |

Allowed and seen: `clients2.google.com`, `update.googleapis.com` and `edgedl.me.gvt1.com`, all
three component updater and all three already on the allowlist. Stage 1's run named only the
first; the doubled window (120 s against 60 s) is the likely reason the other two appeared, since
the component updater's first check is on a delay. No allowlist change was needed for them.

