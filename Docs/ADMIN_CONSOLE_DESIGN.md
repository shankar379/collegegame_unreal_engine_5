# Campus Combat Admin Console: Design

As of 2026-10-15. Status: **draft for owner review**, no admin screens built yet. The app scaffold (React + Vite + TypeScript) is at `campuscombat-backend/admin`.

## Summary

The console turns the numbers the game already reads from Postgres into things the owner can change without a release. The backend is already built for this:

- `engagement_config` is a jsonb key/value table retuned live (`PUT /v1/admin/engagement-config/:key`).
- `catalog_items` holds guns, packs, titles, cosmetics and decor under one `kind` column.
- `wallet_ledger` is an append-only money trail.
- `audit_log` is append-only and already written by `lib/audit.js` on every admin call.
- Migration `020_avatars` + `lib/storage/s3.js` already prove "bytes in S3, served by the API, cached by ETag on the phone", which remote art needs.

So this is not a new system. It is a UI, an admin account model with roles, about nine new tables, and one new game-facing endpoint.

Changes reach players in three layers:

| Layer | Examples | New APK needed? |
| --- | --- | --- |
| Live config | Prices, rewards, event rules, schedules, map rotation, modes on/off, maintenance, minimum version | No, live at next login or refresh |
| Remote images | Event banners, store art, news, loading screens, item icons | No, downloaded and cached by the game |
| Content patches (phase D) | New 3D models, outfits, map changes | Once, to add the downloader; then patches download in game |

Binding constraints:

- The API goes through a **free ngrok tunnel limited to 20,000 requests a month** until the AWS Mumbai move, so every game-facing read must be ETagged and cached on the phone.
- The APK is UE 5.8 Android with `UsePakFile=True`, `bUseIoStore=True`, `bGenerateChunks=False` and no ChunkDownloader, so **today no 3D asset can change without a new APK**.
- `CampusCosmetics.h` deliberately ignores the API's `art` field: the client catalog is the authority on meshes, tints, decor and anchors. The console can enable, price, order, schedule and reward **ids the APK already knows**, plus any **2D image** fetched at runtime. Everything beyond that is phase D.
- Admin auth today is one static `ADMIN_TOKEN` (optional `ADMIN_ALLOW_CIDR`). It becomes break-glass only.

The console must never become a second source of truth: it edits the tables the game already reads.

## Architecture

1. **Console app** (`campuscombat-backend/admin`, React + Vite + TS): a static bundle, served at `/admin` on the same origin as `/v1` (Caddy in production; Vite dev server with a `/v1` proxy locally). Same origin: no CORS, one TLS cert, works through the ngrok tunnel.
2. **Fastify API**: `routes/admin.js` grows from 9 routes to the full surface; `lib/adminauth.js` accepts an admin session besides the static token.
3. **Postgres 17**: system of record for config drafts, catalog, sanctions, audit.
4. **Redis 7**: the published live-config blob and revision, leaderboards, rate limits, analytics cache. A cache, never the verdict.
5. **S3 / LocalStack**: console-uploaded images under `content/` (beside the avatars bucket). Same code path for LocalStack and AWS; only `S3_ENDPOINT` differs.
6. **Content reader**: `GET /v1/content/:key`, a byte proxy with strong ETag + `immutable` (copied from `routes/avatars.js`). Becomes a CloudFront origin on AWS without a client change.
7. **Push worker**: a separate process (like `workers/matchmaker.js`) draining a `push_jobs` outbox to FCM.
8. **Metrics roller**: a nightly job writing `daily_metrics`.
9. **Game client** (`CampusBackend.cpp`): one new call on lobby entry (`GET /v1/live-config`, ETagged) and `CampusRemoteImage`, a generalisation of `CampusAvatar`'s fetch + decode + disk cache.

```
Admin browser --(/admin static, /v1/admin/*)--> Caddy :443 --> Fastify API
Fastify API <--> Postgres 17   (system of record)
Fastify API <--> Redis 7       (live-config blob + revision, rate limits, boards)
Fastify API <--> S3/LocalStack (image bytes)
Fastify API  --> audit_log     (append-only, every mutation)
Push worker  --> Postgres (push_jobs) --> FCM --> Android phones
Metrics roller --> reads match_results/orders, writes daily_metrics
Google Play RTDN --> /v1/store/google/rtdn (exists)
Game phone --> ngrok --> Caddy --> API   (GET /v1/live-config mostly 304, GET /v1/content/:key immutable, GET /v1/store)
Phase D (dashed): Admin --> content_releases --> S3 content/chunks/ --> CloudFront --> Game phone (chunk mount)
```

## Screens

Roles: **O** owner, **M** moderator, **C** content editor.

| Screen | What admins do | Roles |
| --- | --- | --- |
| Dashboard | DAU, live sessions, queue depth, today's revenue, open flags/reports, maintenance state | O M C |
| Live Config | Edit a draft of client config, diff against published, Publish (bumps revision) | O |
| Events | Create and schedule a live event: window, label, banner, modifiers (XP/points multiplier), rewards | O C |
| Content & Art | Upload PNG/JPEG, see where each image is used, replace (new version = new URL), retire | O C |
| Store & Economy | Edit `catalog_items` (gun, cosmetic, decor, pack, offer, bundle), prices, sales, rarity, order, Play product id | O C |
| Player Search | Search by public id, name or email | O M |
| Player Detail | Wallet, items, equipped, match history, sessions, sanctions, notes; grant/refund Diamonds; grant/revoke items | O (money), M (read + sanction) |
| Moderation Queue | Player reports and feedback: triage, ban/mute with expiry, resolve | O M |
| Anti-Cheat Review | `anti_cheat_flags`: dismiss, exclude match, ban (API exists today) | O M |
| Matches & Modes | Enable/disable modes, map rotation and weights, BR schedule and day/night, player caps | O |
| Live Ops | Live battle sessions, rooms, queue depth per mode and region; force-end a stuck session | O M |
| Seasons & Leaderboards | Create periods, close/compute/settle, rebuild boards, global and per-college | O |
| Announcements | News, in-lobby notice, maintenance window, minimum app version | O C |
| Push | Compose a campaign, pick an audience, send or schedule, see delivery counts | O |
| Analytics | DAU/WAU, matches, revenue (Play + Razorpay), D1/D7/D30 retention, top items, crashes | O M C |
| Admins & Audit | Create admins, set roles, reset passwords, read the audit log (read-only) | O |

## Database

New migrations from **022**. Reused unchanged: `catalog_items`, `player_items`, `wallets`, `wallet_ledger`, `idempotency_keys`, `audit_log`, `event_periods`, `period_rewards`, `anti_cheat_flags`, `engagement_config`, `colleges`, `match_results`, `battle_sessions`, `orders`, `feedback`. Every migration ships `.up.sql` and `.down.sql`.

| Migration | Table / change | Key columns | Purpose |
| --- | --- | --- | --- |
| 022 | `admin_users` | `id`, `email citext unique`, `password_hash` (scrypt via `lib/passwords.js`), `role` (owner, moderator, content_editor), `totp_secret`, `disabled_at`, `created_by` | Real admin accounts |
| 022 | `admin_sessions` | `id`, `admin_id`, `token_hash unique`, `expires_at`, `revoked_at`, `ip`, `user_agent` | Sessions; only a hash of the cookie is stored |
| 022 | `audit_log` + `actor_admin_id`, `target_player_id` | index `(actor_admin_id, at desc)` | Who did it, not just that it happened |
| 023 | `live_config` | `key pk`, `draft jsonb`, `published jsonb`, `published_at`, `published_by`, `revision` | Draft then publish for client-visible config |
| 023 | `live_events` | `id`, `label`, `starts_at`, `ends_at`, `state`, `banner_key`, `modifiers jsonb`, `rewards jsonb`, `period_id` | Scheduled events the lobby shows and rewards from |
| 023 | `announcements` | `id`, `kind` (news, notice, maintenance), `title`, `body`, `starts_at`, `ends_at`, `min_build`, `active` | News, notices, maintenance banner |
| 024 | `content_assets` | `key pk`, `s3_key`, `sha256`, `bytes`, `width`, `height`, `content_type`, `version`, `active`, `uploaded_by` | Uploaded images; `version` moves phones to new bytes |
| 024 | `catalog_items` + `image_key`, `sale_price_diamonds`, `sale_ends_at`, `visible_from`, `visible_until`, `tags` | | Remote tile art, sales, scheduled visibility |
| 025 | `game_modes`, `game_maps`, `map_rotation` | enabled, sort, team sizes, map ids, max players, window; rotation weights and dates | Replaces the hard-coded `MAPS` in `routes/rooms.js` |
| 025 | `br_schedule` | `days`, `start_ist`, `end_ist`, `time_of_day` (day, night, auto), `enabled` | BR windows and day/night |
| 026 | `player_sanctions` | `player_id`, `kind` (ban, mute, shadow_mute), `reason`, `expires_at`, `created_by`, `lifted_at` | Sanction history; `players.banned_until` stays the fast copy |
| 026 | `player_reports` | `reporter_id`, `target_id`, `reason`, `room_code`, `state`, unique per reporter/target/day | In-game reports |
| 026 | `player_notes` | `player_id`, `admin_id`, `note`, `at` | Moderator notes |
| 027 | `device_tokens` | `token pk`, `player_id`, `platform`, `last_seen_at` | FCM targets |
| 027 | `push_campaigns`, `push_jobs` | campaign: title, body, deep link, segment, state, schedule; job: token, state, attempts, error | Outbox, drained at-least-once |
| 028 | `daily_metrics` | `day`, `metric`, `value`, `detail`, pk `(day, metric)` | Pre-rolled analytics |
| 028 | `crash_reports` | `day`, `build`, `signature`, `count`, `source` | Play Console crash summary |
| 029 | `admin_approvals` | `action`, `payload`, `requested_by`, `approved_by`, `state`, `expires_at` | Two-person rule for large Diamond grants |
| 030 | `content_releases`, `content_chunks` | release: platform, `min_build`, manifest, version, state; chunk: id, s3 key, sha1, bytes | Phase D chunk releases with rollback |

## API

All admin routes are under `/v1/admin`, keep the existing `requireAdmin` + rate limit, and every change writes one `audit_log` row in the same transaction.

| Method and path | Purpose | Role |
| --- | --- | --- |
| `POST /v1/admin/auth/login` | Email + password + TOTP gives an HttpOnly session cookie | any |
| `POST /v1/admin/auth/logout`, `GET /v1/admin/me` | End session; who am I | any |
| `GET/POST/PATCH /v1/admin/admins[/:id]` | Manage admins and roles | O |
| `GET /v1/admin/audit` | Read the trail (keyset pagination) | O M |
| `GET /v1/admin/live-config`, `PUT .../:key`, `POST .../publish` | Draft, diff, publish | O |
| `GET/POST/PATCH /v1/admin/events[/:id]`, `POST .../:id/publish` | Live events and rewards | O C |
| `POST/GET /v1/admin/content`, `DELETE .../:key` | Upload, list, retire art | O C |
| `GET/POST/PATCH /v1/admin/catalog[/:id]`, `POST .../:id/sale` | Store and economy | O C |
| `POST /v1/admin/players/:id/wallet` (Idempotency-Key) | Grant or refund Diamonds / Room Cards | O |
| `POST/DELETE /v1/admin/players/:id/items/:itemId` | Grant or revoke an item | O |
| `GET /v1/admin/players?q=`, `GET .../:id` (+ ledger, items, matches, sessions) | Search and profile | O M |
| `POST /v1/admin/players/:id/sanctions`, `DELETE /v1/admin/sanctions/:id` | Ban, mute, lift | O M |
| `GET /v1/admin/reports`, `POST .../:id/resolve` | Moderation queue | O M |
| `GET/PUT /v1/admin/modes`, `PUT .../maps/rotation`, `PUT .../br-schedule` | Modes, rotation, BR schedule | O |
| `GET /v1/admin/live/sessions`, `/live/rooms`, `/live/queues`, `POST .../sessions/:id/end` | Live ops | O M |
| `POST /v1/admin/periods`, existing `/periods/:id/{close,compute,settle}`, `/leaderboards/:id/rebuild` | Seasons and boards | O |
| `GET /v1/admin/metrics/overview`, `/revenue`, `/retention`, `/items`, `/crashes` | Analytics | all |
| `POST /v1/admin/push/campaigns`, `POST .../:id/send` | Push campaigns | O |
| `PUT /v1/admin/maintenance` | Maintenance + message + minimum build | O |
| `GET/POST /v1/admin/content-releases`, `POST .../:id/activate`, `.../rollback` | Phase D releases | O |

Game-facing:

| Method and path | Purpose | Caching |
| --- | --- | --- |
| `GET /v1/live-config` | One blob: modes, maps, rotation, BR schedule, live event, announcements, maintenance, minimum build, store order, image versions, revision | ETag `"lc-<revision>"`, `max-age=60`, 304 on match |
| `GET /v1/config` | Unchanged, for older builds | as today |
| `GET /v1/content/:key?v=N` | Image bytes from S3 | ETag + `max-age=31536000, immutable` |
| `GET /v1/store` | Unchanged shape, plus `image_url`, sale fields | as today |
| `GET /v1/announcements?since=` | News list | ETag, `max-age=60` |
| `PUT /v1/me/device` | Register the FCM token | no-store |
| `POST /v1/players/:id/report` | In-game report | Idempotency-Key |
| `GET /v1/content/manifest?platform=android&build=N` | Phase D chunk manifest | ETag, `max-age=300` |

Maintenance mode returns `503 MAINTENANCE` with `Retry-After` on every route except health, config, live-config and admin, so the game can show why it is locked.

## Game integration

1. **Live config on lobby entry.** `UCampusBackend::FetchLiveConfig()` runs once per app start on lobby entry and after a resume older than 10 minutes, with `If-None-Match` from the saved revision. Compiled-in values stay the fallback. The response can only enable, disable, order, price, schedule and relabel ids this build knows; `CampusCatalog` and `CampusCosmetics::OutfitFor` already clamp unknown ids.
2. **Remote 2D art.** `CampusRemoteImage` (generalised from `CampusAvatar`): same HTTP fetch with the ngrok header, same decode to a texture, same memory and disk cache, never blocks, caller keeps its fallback. `SCampusArtPlate` and store/wardrobe tiles prefer the remote picture when ready. Store tiles, map banners, event banners and news change from the console with no APK.
3. **Maintenance, minimum version, announcements, push.** `503 MAINTENANCE` shows a lock screen with the server's message; a minimum build above this build shows "update required". Announcements appear in `SCampusNoticeStack` (`CampusNoticeUI.cpp`). The FCM token is registered with `PUT /v1/me/device`.

Still needs a new APK today: every mesh, material, level, decor recipe, outfit tint, gun stat curve, and anything in `CampusCosmetics.h`, `CampusGunMeshes.h` or `CampusCatalog`. Phase D moves that line.

## Security and audit

- A player login can never reach `/v1/admin/*`.
- Admin sessions: random 32-byte token, only its hash stored, `HttpOnly; Secure; SameSite=Strict` cookie, 12-hour expiry, revocable. Passwords via scrypt; TOTP required for owners.
- The static `ADMIN_TOKEN` becomes break-glass only (owner-level, loudly audited). `ADMIN_ALLOW_CIDR` still applies.
- CSRF: same origin plus a required `X-Admin-Request: 1` header on every change.
- Roles are enforced on the server per route, not by hiding buttons. Content editors cannot touch wallets, sanctions or admins; moderators cannot touch money or the catalog.
- Money: grants need an Idempotency-Key, write `wallet_ledger` with `reason='admin_grant'`, and large grants need a second owner's approval.
- Personal data: moderators see masked emails; owners see full emails, and each unmask is audited. No bulk export without owner. The console never deletes accounts.
- `audit_log` is append-only at the database level (insert and select only).
- Uploads are decoded, size-checked and re-encoded by the server, which chooses the storage key.

## Phases and milestones

Solo developer + AI, starting Monday 2026-10-19 (engineering days).

| Phase | Dates | Days | Ships | Depends on |
| --- | --- | --- | --- | --- |
| A Foundation | Oct 19 - Nov 3 | 12 | Admin accounts, roles, sessions, TOTP, audit; console shell; live config + `GET /v1/live-config`; maintenance, minimum version, announcements; player search and sanctions; game reads live config | nothing |
| B Live ops and economy | Nov 4 - Nov 23 | 14 | Image uploads + `GET /v1/content/:key`; store editor; Diamond grants/refunds with approvals; live events with rewards; modes/maps/BR schedule from the database; moderation queue; remote art in game | A |
| C Insight and reach | Nov 24 - Dec 11 | 14 | Analytics; live sessions/rooms/queues; anti-cheat review; push campaigns; seasons and college boards. In parallel: AWS Mumbai move | A, B |
| D Content patching | Dec 14 - Jan 9 | 15 + 5 buffer | One APK adds chunk downloading; then new models, textures and map art download without an APK, with rollback | C |

Milestones: M1 Oct 23 admin login + audit · M2 Nov 3 maintenance + notices · M3 Nov 12 first remote image in game · M4 Nov 23 store editable end to end · M5 Dec 4 analytics dashboard · M6 Dec 11 AWS Mumbai cutover · M7 Jan 9 first content chunk on a phone.

Even after phase D, these still need a new APK: any C++ change, new plugins, engine upgrades, new global shaders, new Gameplay Tags or classes. Chunks must be cooked with the same engine version as the installed APK.

## Decisions for the owner

1. **Admin login:** keep the one shared token, add real accounts, or use a third-party login? Recommended: real accounts with roles and TOTP; the token stays as break-glass.
2. **Hosting:** serve the console at `/admin` on the same domain as the API. Recommended: yes (no CORS, works through ngrok).
3. **Images:** served through the API now, CloudFront after the AWS move. Recommended: yes.
4. **Config endpoint:** a new `/v1/live-config`, leaving `/v1/config` for old builds. Recommended: yes.
5. **Two config tables:** `engagement_config` for server-side economy, `live_config` for what the game reads. Recommended: keep them separate.
6. **Analytics:** Postgres rollups now, a warehouse or product (ClickHouse, PostHog) only above about 50k daily players. Recommended: Postgres now.
7. **Modes and maps:** the database turns on and orders what the APK defines. Recommended: yes.
8. **Phase D method:** ChunkDownloader pak chunks vs IoStore on-demand (Play Asset Delivery is out: it needs a new app bundle). Recommended: a 2-day test on a real phone during phase C, then choose.
9. **Diamond grant limits:** one owner up to 2,000 Diamonds (about Rs 400), two owners above that, a daily cap per admin, refunds always as ledger entries. Owner to set the numbers.
10. **Razorpay:** keep it off for Android in-app goods; the revenue screen reads both providers. Recommended: yes.
11. **Who sees player emails:** moderators masked, owners full and audited. Add a read-only analyst role now if one is planned.
