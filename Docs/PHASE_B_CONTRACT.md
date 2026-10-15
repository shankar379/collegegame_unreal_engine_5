# Admin Phase B: Shared Contract

As of 2026-10-15. Every agent working on phase B builds against this file. Design background: `Docs/ADMIN_CONSOLE_DESIGN.md`. If something here is wrong, report it; do not silently diverge.

## Ownership (who edits what)

| Workstream | Agent | Owns (creates/edits) | Must NOT edit |
| --- | --- | --- | --- |
| B1 Economy | igris-economy | `api/src/routes/adminrevenue.js`, `api/src/routes/admineconomy.js`, `api/test/admin_economy.test.js`, `admin/src/screens/Revenue.tsx`, `admin/src/screens/Store.tsx`, `admin/src/screens/Approvals.tsx`, migration `029_admin_approvals` | shared files below |
| B2 Content and events | igris-content | `api/src/routes/admincontent.js`, `api/src/routes/content.js`, `api/src/routes/adminevents.js`, `api/src/lib/contentstore.js`, `api/test/admin_content.test.js`, `admin/src/screens/Content.tsx`, `admin/src/screens/Events.tsx`, migration `026_content_assets` | shared files below |
| B3 Modes and moderation | igris-ops | `api/src/routes/adminmodes.js`, `api/src/routes/reports.js`, `api/src/routes/adminreports.js`, `api/test/admin_modes.test.js`, `admin/src/screens/Modes.tsx`, `admin/src/screens/Reports.tsx`, migrations `027_game_modes`, `028_player_reports` | shared files below |
| Review fixes | igris-fix (running) | security fixes, migration `025_admin_totp_step`, test DB isolation (`api/test/helpers.js`, `api/test/test.env`) | phase B files |
| Game | igris-game (later) | Unreal `Source/` | backend |

**Shared files: only the coordinator edits these.** Each agent puts the exact lines it needs in its final report under "Shared-file lines":

- `api/src/app.js` (route registration)
- `admin/src/nav.ts` (sidebar entries), `admin/src/App.tsx` (routes)
- `admin/src/api/types.ts` (agents may put their types in `admin/src/api/<workstream>.ts` instead)
- `migrations/README.md`
- `api/test/helpers.js` (put new helpers in `api/test/helpers_<workstream>.js`)
- `api/src/lib/liveconfig.js` (B2/B3 report the blob keys they add; the coordinator wires them)

## Conventions (all agents)

- Admin routes: declare with `adminRoute(roles, extra)` from `api/src/lib/adminauth.js`. Every mutation writes `adminAudit(...)` from `api/src/lib/audit.js` **inside the same transaction**. Mutations require `X-Admin-Request: 1` (enforced centrally).
- Roles: `ROLES.owner = 'owner'`, `'moderator'`, `'content_editor'`.
- Errors: `errors.*` from `api/src/lib/errors.js`, envelope `{error:{code,message,request_id,details?}}`.
- Validation: zod, closed objects (no unknown keys).
- Money: never edit a balance. Diamonds move only through `wallet_ledger` (existing helpers in `store.js` / wallet lib). Admin money mutations need an `Idempotency-Key` (`api/src/lib/idempotency.js`, scope `admin:<admin_id>`).
- Live config: anything the game reads goes into the published blob through `live_config` keys (`PUT /v1/admin/live-config/:key` + publish). Game-facing reads must be ETagged.
- Migrations: `.up.sql` + `.down.sql`, numbered exactly as assigned above.
- React: data via `@tanstack/react-query`, HTTP via `admin/src/api/client.ts` (`api.get/post/put/patch/del`), reuse `components/State.tsx`, `Modal.tsx`, `toastStore.ts`, `format.ts`, styles in `admin/src/styles.css` (add classes, don't restyle existing ones). `npm run build` and `npm run lint` must pass with zero errors.
- Tests: node test runner, real DB, existing helpers (`signedInAdmin`, `asAdmin`, `auditRows`, ...). Run your own test file; report exact counts.
- Do not restart the API on :8080. Do not git commit.

## B1 Economy

### Revenue (owner only, no migration; reads `orders` + `wallet_ledger` live)

`GET /v1/admin/revenue?from=YYYY-MM-DD&to=YYYY-MM-DD&group=day|week|month`

```json
{
  "currency": "INR",
  "totals": { "gross_paise": 0, "refunded_paise": 0, "net_paise": 0, "orders": 0, "paying_players": 0 },
  "by_provider": [ { "provider": "google_play" | "razorpay", "gross_paise": 0, "refunded_paise": 0, "orders": 0 } ],
  "series": [ { "period": "2026-10-15", "gross_paise": 0, "refunded_paise": 0, "orders": 0 } ],
  "top_items": [ { "item_id": "", "name": "", "orders": 0, "gross_paise": 0 } ],
  "note": "Gross = what players paid. Google Play payout (after fee and tax) is in Play Console."
}
```

Times in IST (`Asia/Kolkata`). Paid = `orders.status='paid'`; refunded = `status='refunded'` or `voided_at` set.

### Store and economy editor (owner, content_editor)

- `GET /v1/admin/catalog?kind=` → `{ items: [CatalogItem] }`
- `POST /v1/admin/catalog` → create; `PATCH /v1/admin/catalog/:id` → edit
- `POST /v1/admin/catalog/:id/sale` `{ sale_price_diamonds, sale_ends_at }`; `DELETE .../sale`
- `CatalogItem` fields: `id, kind, name, slot, rarity, price_diamonds, sale_price_diamonds, sale_ends_at, starter, active, sort, sell_ratio, grants, image_key, visible_from, visible_until, tags`
- Columns `sale_price_diamonds`, `sale_ends_at`, `visible_from`, `visible_until`, `tags`, `image_key` are added by **B2's migration 026** (B1 uses them; coordinate by reading 026 once it exists, or guard reads until then).
- `GET /v1/store` (player-facing) must honour `active`, `visible_from/until` and an active sale (`effective_price_diamonds`).

### Diamond grants/refunds (owner only)

- `POST /v1/admin/players/:id/wallet` `{ currency: "diamonds" | "room_cards", amount: int (+grant / -refund), reason: string, ref?: string }` + `Idempotency-Key`. Writes `wallet_ledger` with `reason='admin_grant'` / `'admin_refund'`, `ref=<audit id>`.
- Above `ADMIN_GRANT_APPROVAL_LIMIT` (default 2000 Diamonds) → creates `admin_approvals` row, returns `202 { approval_id, state: "pending" }`.
- Migration `029_admin_approvals`: `admin_approvals(id uuid pk, action text, payload jsonb, requested_by uuid, approved_by uuid, state text CHECK in ('pending','approved','rejected','expired'), created_at, decided_at, expires_at)`.
- `GET /v1/admin/approvals?state=pending`, `POST /v1/admin/approvals/:id/approve`, `POST .../reject`. Approver must be a **different** owner. Daily cap per admin: `ADMIN_GRANT_DAILY_CAP` (default 10000).

## B2 Content and events

### Image uploads (owner, content_editor)

- Migration `026_content_assets`: `content_assets(key text pk, s3_key text, sha256 bytea, bytes int, width int, height int, content_type text, version int, active bool, uploaded_by uuid, created_at, updated_at)` **plus** `catalog_items` columns `image_key text, sale_price_diamonds int, sale_ends_at timestamptz, visible_from timestamptz, visible_until timestamptz, tags text[] default '{}'`.
- Keys: `^[a-z0-9_]+(/[a-z0-9_]+)*$`, e.g. `store/gun_m4a1`, `event/diwali_banner`, `map/varsity_valley`, `news/patch_1`.
- `POST /v1/admin/content?key=<key>` raw body `image/png` or `image/jpeg`, max 2 MB, max 2048x2048; decoded and re-encoded server-side (reuse `api/src/lib/avatar.js` approach); new upload of an existing key bumps `version`.
- `GET /v1/admin/content` list; `DELETE /v1/admin/content/:key` retires (active=false).
- Game-facing `GET /v1/content/:key?v=<version>` → bytes, strong ETag, `Cache-Control: public, max-age=31536000, immutable`.
- Live-config blob key `content` → `{ "base_url": "/v1/content", "assets": { "<key>": <version> } }` (report it; coordinator wires it).

### Live events (owner, content_editor). Table `live_events` already exists (migration 023)

- `GET/POST/PATCH /v1/admin/events[/:id]`, `POST /v1/admin/events/:id/publish`, `POST .../end`
- Event: `id, label, starts_at, ends_at, state ('draft','scheduled','live','ended'), banner_key, modifiers {xp_multiplier, points_multiplier}, rewards [{ kind: "diamonds"|"room_cards"|"item", item_id?, amount, condition: { mode?: string, matches: int } }], period_id`
- Live-config blob key `events` → active + upcoming (next 7 days) events (report it).
- Reward claim: `GET /v1/events/:id/progress`, `POST /v1/events/:id/claim` (Idempotency-Key, once per player per reward, ledger-safe).

## B3 Modes, maps and moderation

### Modes and maps (owner)

- Migration `027_game_modes`: `game_modes(id text pk, enabled bool, sort int, team_sizes int[], map_ids text[], max_players int, window jsonb)`, `game_maps(id text pk, enabled bool, name text, sort int)`, `map_rotation(mode_id, map_id, weight int, valid_from, valid_until)`, `br_schedule(id serial pk, days int[], start_ist time, end_ist time, time_of_day text CHECK in ('day','night','auto'), enabled bool)`.
- Seed with the ids the game knows: modes `TeamClash`, `QuickMatch`, `BattleRoyale`; maps `Classic`, `CampusMap`, `VarsityValley` (from `CampusMapArt.cpp` `CampusCatalog`).
- `GET/PUT /v1/admin/modes`, `PUT /v1/admin/maps/rotation`, `GET/PUT /v1/admin/br-schedule`.
- `routes/rooms.js` hard-coded `MAPS` should read `game_maps`/`game_modes` (enabled only) with the current values as the fallback.
- Live-config blob key `modes` → `{ "modes": [{ id, enabled, sort, team_sizes, map_ids, max_players, window }], "maps": [{ id, enabled, sort }], "br_schedule": [...] }` (report it).

### Reports and moderation (owner, moderator)

- Migration `028_player_reports`: `player_reports(id uuid pk, reporter_id uuid, target_id uuid, reason text CHECK in ('cheating','abuse','name','afk','other'), detail jsonb, room_code text, state text CHECK in ('open','actioned','dismissed') default 'open', resolved_by uuid, resolved_at, created_at, game_day date)`, unique `(reporter_id, target_id, game_day)`; `player_notes(id, player_id, admin_id, note text, at)`.
- Player-facing `POST /v1/players/:id/report { reason, detail?, room_code? }` + Idempotency-Key, rate-limited, cannot report self.
- `GET /v1/admin/reports?state=open`, `POST /v1/admin/reports/:id/resolve { action: 'dismiss'|'warn'|'mute'|'ban', expires_at?, note? }` (mute/ban go through the existing `player_sanctions` path from `routes/adminplayers.js`).
- `GET/POST /v1/admin/players/:id/notes`.

## Game side (later, after the game review fixes)

Owned by one game agent, reads the blob keys above:

- `content` → `CampusRemoteImage` (generalised from `CampusAvatar`), `SCampusArtPlate` prefers the remote picture.
- `events` → lobby event banner + progress/claim.
- `modes` → enable/order modes and maps in map select (compiled catalog stays the fallback).
- Report button on player cards → `POST /v1/players/:id/report`.
