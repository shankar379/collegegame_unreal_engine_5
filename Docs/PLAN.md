# Campus Combat — Plan & Status

Last updated: 2026-10-12. One place to remember what is done, what is being built, and every decision taken.

Repos: game `github.com/shankar379/collegegame_unreal_engine_5` (public — never commit secrets), backend `github.com/shankar379/campuscombat-backend`.

---

## 1. How it runs today (free)

```
Phone (any network) --HTTPS/WSS--> ngrok (dante-morphologic-momently.ngrok-free.dev) --> laptop :8080 API
                                                                                       --> Postgres (Docker :5433) + Redis (:6380)
Match traffic: host phone <--> Epic Online Services P2P relay <--> other phones
```

- **Start the server** (laptop must stay on and awake):
  1. `docker compose up -d` in `campuscombat-backend`
  2. `node --env-file=../.env src/server.js` in `campuscombat-backend/api`
  3. `ngrok http 8080 --url dante-morphologic-momently.ngrok-free.dev`
  4. Check: `https://dante-morphologic-momently.ngrok-free.dev/v1/health` → `{"status":"ok",...}`
- **ngrok free limit:** 20,000 requests/month → roughly 2,000 player sessions. **More than ~30 regular testers → move to Oracle Cloud Always Free** (₹0; deploy files already in `campuscombat-backend/deploy`, guide `docs/DEPLOY.md`).
- **Epic (EOS) values** live only in the local `Config/DefaultEngine.ini` (skip-worktree, not on GitHub).
- **Demo logins:** `test1@gmail.com` / `test2@gmail.com`, password `123456` (seed: `node --env-file=../.env scripts/seed-demo.js`).
- **Build:** `BuildAndroid.bat` → APK in `C:\UE\Projects\CampusCombat\AndroidBuild\` (keep only the latest).

### Rules learned the hard way
- Unreal ini treats `//` as a comment → **quote URLs** (`Key="https://..."`).
- **Never stop the backend during a build** while people are playing; after any build check the public `/v1/health`.
- **Test the full flow through ngrok** (sign in → lobby → room → join) before handing over an APK.
- Only **one Unreal build at a time** (PC memory).

---

## 2. Done ✅

| Area | What |
|---|---|
| Networking | ngrok HTTPS tunnel, EOS P2P relay for cross-network matches, lag compensation, hit zones, damage falloff, fall damage |
| Sign-in | Guest + email login/register, 18+ only (tick, no DOB) |
| Lobby | Free Fire angular style, squad line-up, characters on the ground in Solo/Duo/Squad, settings gear, logo + app icon |
| Match HUD | Alive/Kill chips, ping, HP bar, match info opens from the **timer tap**, voice (team only), breakable doors, sky dome |
| Combat feel | Enemy glitch fix (phone-friendly smoothing, eased remote aim, lighter host) |
| Map select | Free Fire-style screen: CAMPUS + 2 locked "coming soon" maps, mode cards, CONFIRM |
| Billing | Google Play Billing (backend verify + refunds, game client); Razorpay off. Needs Play Console products + license key |
| Engagement backend | Weekend leaderboard, missions, login streak, anti-cheat gate (observe mode), colleges list |
| Social backend | Friends (requests, block, recent players), parties + invites, WebSocket live events, presence, match summary + Player of the Match, profile/XP/levels/likes/milestones |
| Deploy | Dockerfile, Caddy HTTPS, GitHub Actions CI + deploy, backups (ready for Oracle/AWS later) |

---

## 3. In progress ⏳

### 3.1 Friends screens (match Free Fire references in `Downloads/*.jpg.jpeg`)
1. Friends icon top-right in the lobby (red dot for requests/invites).
2. Friends page: left yellow tabs **GAME FRIENDS / ADD**, counter "Game Friends x/100", rows = avatar + name plate + LV + status (**ONLINE** orange or "4 HOURS AGO") + actions; yellow **REQUESTS** button bottom-left.
3. ADD tab: search "nickname or ID", results with **+** → becomes **REQUESTED**; suggested players when empty.
4. Lobby right-side friends panel before START (online first, **+** invite, collapsible).
5. Backend additions: 9–10 digit **public player ID**, `GET /v1/players/search`, `last_seen_at`, `GET /v1/players/suggested`.

### 3.2 Party invites
- Duo/Squad **+ INVITE** → friends list. Friend **in a match** → invite disabled ("IN MATCH").
- Invite card slides in from the **right** (250 ms), stops, shows sender + Duo/Squad + countdown, **ACCEPT / REJECT**; reject/expiry slides out right (200 ms); cards stack.
- Accept while in another squad → confirm "Leave your squad and join X?".
- Leader leaves → next member promoted. Leader starts → party auto-joins the room.
- Only while the app is open (no push notifications).

### 3.3 Post-match
- Few-second **Player of the Match** screen (most kills; tie → fewer deaths → more headshots), all players' stats, **Add friend** buttons.

### 3.4 Profile (Free Fire style)
- Tap avatar/name → character on the left (drag to rotate), tabs **SHOWCASE / PROFILE / HISTORY**.
- Level + XP (server-side, only from counted matches), public ID with copy, likes (1 per viewer per day, no rewards), stats, milestones, title, best weekend rank (no fake rank badge), match history, SHARE = copy text.

### 3.5 Drivable cars (`Docs/VEHICLES_DESIGN.md`)
- Lightweight custom car (not Chaos), 3 cars on the campus, **4 seats**, driver can't shoot, top speed **43 km/h**.
- **600 HP** (engine 1.0 / body 0.75 / roof 0.5 / glass 0.25), wheels 100 HP, 0 HP → burn 3 s → explode (85 dmg to 4.5 m, killer's teammates spared).
- Bullets pass windows (chest/head exposed), doors protect legs. Roadkill from 14 km/h, lethal above ~36 km/h, no friendly fire. No fuel. Wreck stays until round end.

---

## 4. Next / later 📋
- Full review (Bellion) + end-to-end ngrok test, then one APK with everything above.
- Ranked tiers (only when there is public matchmaking).
- Real map/character art (map banners and name plates are generated placeholders).
- Unique characters: 3–4 meshes on the mannequin skeleton (~15k tris each).
- Move hosting to Oracle Free when testers > ~30; EOS dedicated server later.
- Play Console: create the 14 products, license key, internal testing track.
- Engagement: switch `scoring_mode` from observe to live after one weekend of data.

## 5. Decisions log
| Topic | Decision |
|---|---|
| Hosting | PC + ngrok now; Oracle Always Free later (never "Upgrade to Pay As You Go") |
| Age | 18+ only, self-declared tick |
| Lucky wheel / loot boxes | No (India gaming law + Play policy) |
| Weekend board | Fri 17:00 → Mon 05:00 IST, 2+ players to count (soft launch) |
| Colleges | Fixed list; hidden on boards unless the player turns it on |
| Party | Promote on leader leave; confirm before switching squads; no invites to players in a match |
| Profile | Showcase (not photo gallery); XP only from counted matches; 1 like/day |
| Scope zoom | Starts at 2x (owner may change to 1x) |
| Cars | Driver can't shoot, 43 km/h, 4 seats, 600 HP |

---

## 6. Night of 2026-10-12 → status at 00:05 (13 Oct)

Backend (pushed to `main`, live, 191 tests green): battle-royale matchmaking (`/v1/br/*`), friends search + public IDs, party/invites/WebSocket, profile/XP, payments fixes (refund over-claw, wallet credits, multi-quantity, already-owned gun), guns bought with Diamonds + attribute bars, demo people + demo bots scripts.

Game (branch `wip/battle-royale-night`, **NOT compile-verified** — the integration compile was killed by low memory):
- BR mode: `CampusBRGameMode/Zone/Plane` (waiting deck, countdown, plane, jump/parachute, 5-phase zone, virtual combatants to 50, BOOYAH)
- AI bots: `CampusBot` (missing-include fix applied)
- BR client: `CampusBR`, `CampusBRUI`, `CampusBRHud`, map select BR card + DAY/NIGHT
- Map: `Scripts/varsity_layout.py` + `build_varsity_valley.py` (not generated yet), `CampusDayNight`
- Weapons + loot: `CampusWeapons`, `CampusLoot`, bought-gun mods; gun meshes `Scripts/build_guns.py` (not generated yet), `CampusGunMeshes.h`
- Cars: finished + reviewed (compiled clean in an earlier pass)
- Store redesign `CampusStoreUI.cpp`; UPI pending fix in `CampusPlayBilling.cpp`
- Docs: BATTLE_ROYALE_DESIGN, CUSTOM_ROOM_DESIGN, GUN_ATTRIBUTES_DESIGN, VEHICLES_DESIGN

Next steps (in order, one Unreal job at a time, close other apps first — 8 GB RAM):
1. `Build.bat CollegeCampusGameEditor Win64 Development -Project=...uproject -WaitMutex` → fix errors (known risk: `CampusWeapons::ModsForLevel` arity in `CampusCharacter.cpp` ApplyWeapon, C4458 shadowing there)
2. Run `Scripts/build_guns.py`, then `Scripts/build_varsity_valley.py` via UnrealEditor-Cmd pythonscript
3. Merge `wip/battle-royale-night` into `main`, then `BuildAndroid.bat`, install
4. Open questions: ranked vs fun-toggle rooms; flat stats in BR; gun price/power rebalance (AK47 2.7× starter DPS)

---

## 7. Status 2026-10-13 (afternoon)

Done and on `main` (APK `AndroidBuild/CampusCombat-final-1400.apk`, installed on the test phone):
- Touch fix in matches (BR HUD no longer blocks taps), lobby flicker fixes, scrollable ONLINE list
- Lobby line-up: player centre-left and in front, teammates behind; feet on the floor; faces the camera (mesh offset -90 on the actor yaw)
- Store no longer rebuilds every frame; drag turntable needs a real swipe
- Squad: leader leaving disbands the squad; everyone returns to SOLO; LEAVE SQUAD on your own plate
- Battle Session v1 (backend `bc26a5e`): Redis queue per mode x region, matchmaker worker, allocator seam (phone now, dedicated/AWS later)
- Plane model, visible parachute, JUMP -> OPEN CHUTE, auto-open at 40 m, plane at 300 m, 17 drop fixes, decorative parachutists

In progress — Battle Session v2 (PUBG/Free Fire model, `BATTLE_SESSION_DESIGN.md`):
- START -> placed at once into an OPEN session (or a new one) -> waiting area immediately
- Random spawn spots, live PLAYERS x / 50, session id on the HUD
- Backend seals on full or the 25 s cap -> bots -> `bs.sealed` -> 10 s countdown -> plane
- Empty session auto-deleted after 10 s; full session sends new START to the next session
- Works with ONE real player (the owner is the only tester); demo party members who never connect become bots
- Verification: `campuscombat-backend/api/scripts/e2e-battle-session.mjs` (run through ngrok)

Open decisions:
- Dedicated servers need an Unreal engine built from source (launcher engine cannot build Server targets) — needed before AWS
- BR admission check (session ticket) before public release
- Dedicated server needs a server credential for heartbeats/results
- Earlier: ranked vs fun rooms, flat BR gun stats, AK47 balance, room card pricing
