# Campus Combat — Battle Royale Design

> *"I have already seen this system five years out. The foundation must hold today."* — Tusk, Stone of Truth

Author: Tusk (architecture). Date: 2026-10-12. Status: **design, not approved**. No code in this document.
Companion docs: `GAME_PLAN.md` (modes, economy, engagement), `PLAN.md` (what runs today), `EOS_SETUP.md` (P2P relay), `VEHICLES_DESIGN.md`.

---

## 0. Why this exists

The owner wants a Free Fire-shaped battle royale: lobby → Solo/Duo/Squad → auto-matchmake into a **50-player** match → waiting island → plane → jump → parachute → loot → shrinking zone → last team standing.

Our infrastructure cannot host 50 humans. **There is no dedicated server** (an installed UE 5.8 binary build cannot cook a server target), matches run on a **player's phone as listen server** over the **EOS P2P relay**, and the backend is a laptop behind **ngrok free (20,000 requests/month)**.

This document separates **what FF/PUBG/BGMI actually do** (section 1–2) from **what is physically possible for us** (section 3 onward), and gives one buildable plan.

**The one-line answer:** a 50-player match is achievable **as a player experience** — 8–12 humans per phone host, the rest AI, and most of that AI never exists as a pawn. The alive counter says 50. The host simulates ~16–20 bodies.

---

## 1. Constraints (the load-bearing line)

| Constraint | Value | What it forbids |
|---|---|---|
| Match authority | One **player's phone** as UE listen server | No server-side 50-pawn simulation; no server-side anti-cheat beyond the host; host has 0 ping (fairness issue) |
| Transport | **EOS P2P relay** (Epic-relayed UDP) | Relay adds 30–120 ms; bandwidth is the host's mobile uplink |
| Host uplink | Typical Indian 4G: **1–5 Mbps up**, jittery | Hard cap on replicated actors × update rate |
| Host CPU/thermal | Mid-range Snapdragon 6xx/7xx, 6 GB, **thermal throttles in 6–10 min** | Match must end in ≈10 min; bot AI must be nearly free |
| Host volatility | Host is a *player*; can die, rage-quit, get a call, lose signal | **No host migration** in UE out of the box → match dies with the host |
| Backend | Fastify + Postgres + Redis on a laptop, ngrok free **20k req/month** | ≈2,000–2,500 player sessions/month; polling-based matchmaking is impossible |
| Client | Android, UE 5.8, 60 fps target on mid-tier, 30 fps floor | Map streaming budget, no 8×8 km map, no 50 animated skeletal meshes on screen |
| Target match length | **8–10 min** (FF-like, thermal-bound) | PUBG's 30-min 9-phase zone is wrong for us |

### Assumptions (challenge these)
1. Concurrent players at soft launch: **20–60**, peaking on fest evenings. Not 500.
2. Average session: 3 matches. Monthly budget ≈ 2,000 sessions is enough until we move to Oracle Free.
3. We will **not** build host migration this year. Accepted loss.
4. Players will accept bots if bots are not obviously robotic in the first 3 minutes (FF/BGMI have conditioned this).
5. Target device floor: 6 GB RAM, Vulkan, Adreno 6xx class. Host must be at least that.
6. Solo/Duo/Squad all run on the **same** match template; only team assignment differs.

---

## 2. What Free Fire / PUBG Mobile / BGMI actually do

### 2.1 Match shape

| | Free Fire | PUBG Mobile / BGMI (Erangel) | PUBGM (Sanhok) | **Campus Combat (target)** |
|---|---|---|---|---|
| Players | 50 | 100 | 100 | **50 nominal** (8–12 human) |
| Map size | ~2×2 km (Bermuda) | 8×8 km | 4×4 km | **1.5×1.5 km** |
| Match length | ~10 min | ~28–32 min | ~18–20 min | **8–10 min** |
| Teams | Solo / Duo / Squad(4) | Solo / Duo / Squad(4) | same | same |
| Zone phases | 5–6 | 9 | 9 (dynamic) | **5** |
| Bots | Heavy in low tiers | Heavy ≤ Silver, thin to Gold, ~none at high MMR | same | **Always, by design** |
| Win call | **"BOOYAH!"** | "Winner Winner Chicken Dinner" | same | **"FIRST RANK!"** / Topper |

### 2.2 Plane, jump, parachute (observed behaviour, used as our reference)

| Beat | Free Fire | PUBG Mobile | Our target |
|---|---|---|---|
| Pre-plane holding area | Lobby island, players run and shoot freely, no damage | Spawn island, free movement, loose weapons, ~60 s | **Waiting Quad** — free movement, no damage, voice on |
| Plane entry → jump enabled | ~10 s after boarding | ~10 s after boarding | **3 s** (shorter match) |
| Flight path | **Random straight line across the island**, variable entry edge and angle | Random straight chord, shown as a line on the map | **Random chord through the map, biased to pass within 400 m of the campus core** |
| Jump window | Any time while over land; eject button appears | Any time; auto-eject at the end of the path | **~45 s window**; forced eject at path end |
| Freefall | ~50 m/s normal, nosedive faster | ~60 m/s, nosedive ~126 m/s (dive) | **Fall 48 m/s, nosedive 78 m/s** |
| Chute deploy | Auto near ground; manual earlier | Auto at ~**150 m** above ground; manual any time | **Auto at 130 m**, manual allowed above that |
| Chute descent | ~10 m/s, steerable | ~10–12 m/s, steerable, nose-down to glide | **10 m/s, steerable, glide ratio ~2.5:1** |
| Landing | Instant control, no fall damage | Instant control, no fall damage | **No fall damage from a deployed chute**; existing fall-damage system applies if chute is cut |

Controls (mobile convention we should copy, not invent): one **JUMP/EJECT** button while in plane → during freefall, **left thumb = look/steer**, right side shows **altitude + distance-to-ground**, a **dive** toggle, and after deploy a **cut chute** option. Map tap to set a landing marker before jumping (FF does this; it massively reduces first-minute confusion).

### 2.3 Zone (authoritative PUBG numbers, for calibration)

PUBG Erangel playzone, per Epic/PUBG wiki data (damage is **% of max HP per second**, increases with distance outside):

| Phase | Delay (s) | Shrink starts after (s) | Safe circle Ø (m) | Shrink rate (m/s) | DPS |
|---|---|---|---|---|---|
| 1 | 120 | 270 | 3994 | 24.7 | 0.4% |
| 2 | 0 | 180 | 2397 | 13.3 | 0.6% |
| 3 | 0 | 130 | 1318 | 12.0 | 0.8% |
| 4 | 0 | 120 | 725 | 9.9 | 1% |
| 5 | 0 | 100 | 363 | 6.0 | 3% |
| 6 | 0 | 90 | 181 | 6.0 | 5% |
| 7 | 0 | 70 | 91 | 3.0 | 7% |
| 8 | 0 | 60 | 45 | 1.5 | 9% |
| 9 | 30 | 30 | 0 | 1.5 | 11% |

Sanhok compresses the same curve into ~half the time and makes shrink speed **react to the number of players alive** — that is the model we copy, because our player count is small and our match is short. Free Fire runs 5–6 phases over ~10 minutes with the first zone appearing around the 3rd–4th minute.

**Three lessons we take:**
1. Early phases are a *gentle* tax (0.4–1% HP/s) — survivable, heal-through-able. Only phase 5+ is lethal.
2. Each circle is roughly **55–60% of the previous diameter** — a clean geometric curve, not hand-placed.
3. Damage scales with **how far outside** you are, not just phase. This is what makes a long run back feel desperate.

### 2.4 Loot tiers (industry convention)

| Tier | Colour | FF/PUBGM examples | Where it spawns | Share of ground loot |
|---|---|---|---|---|
| Common (I) | Grey/white | pistol, lvl 1 vest/helmet, bandage | everywhere | ~45% |
| Uncommon (II) | Green | SMG, lvl 2 armour, first aid | buildings, small towns | ~30% |
| Rare (III) | Blue | AR, 2x–3x scope, med kit | named locations | ~17% |
| Epic (IV) | Purple | lvl 3 armour, 4x–6x, DMR | hot drops, loot rooms | ~6% |
| Legendary | Gold | AWM/sniper, lvl 3 helmet, high-end skins | **airdrop only** | ~2% + airdrops |

Airdrops: PUBG/FF both drop 1–3 crates per match, flagged with a plane + smoke, carrying guns that **cannot** be found on the ground. This is the single highest-value engagement lever per unit of engineering cost.

### 2.5 Bots in low tiers (what we are already allowed to do)

Developers do not publish bot ratios; what is consistently reported:
- Ratio is **dynamic** — heaviest at Bronze, thinning through Silver/Gold, absent at top MMR.
- New accounts face overwhelmingly bots in their first games (community estimates ~80% for games 1–5, ~60% for 6–10).
- Bots exist for two reasons: **queue time** and **new-player retention**. Both apply to us with far more force.
- Bots are *detectable* — they strafe poorly, have bad aim, rarely crouch, never loot efficiently. Players tolerate this at low tiers.

**This is our licence.** The entire mobile BR genre fills lobbies with bots. We are not cheating the player; we are matching the genre's own floor.

### 2.6 Scoring / Booyah

Free Fire's per-match rank delta is a sum of three parts: **placement score + battle score (kills/damage) + mission score**. A Booyah (1st place) tops out around +19 rank points; 1000+ damage is worth up to ~10. PUBG Mobile uses a similar placement-weighted curve plus kill points, with the top-10 cutoff as the "you did not lose" threshold.

**Design rule to steal:** placement must be worth more than kills, but kills must be worth enough that passive hiding is not optimal. Roughly **60/40 placement:combat**.

### 2.7 Matchmaking queue times

Real queue times in FF/BGMI are **2–15 seconds** because they bot-backfill instead of waiting for humans. PUBG Mobile in thin regions/hours degrades to 30–90 s. No mainstream mobile BR makes a player wait more than ~45 s.

**This is the number that decides our architecture.** With 20–60 concurrent players we will *never* assemble 50 humans. Bot backfill is not a shortcut — it is the only way the queue is ever short.

---

## 3. What is realistic for us

### 3.1 The central problem

A UE listen server on a mid-range Android phone must simultaneously: render the host's own view at 60 fps, run authoritative physics/hit traces for everyone, replicate to every client over mobile uplink, and tick AI for every bot.

Measured expectations for a phone host (to be validated, see §9):

| Load | Mid-tier phone host | Verdict |
|---|---|---|
| 4 human clients + 0 bots | comfortable, 60 fps | today's state |
| 8 human clients | 45–60 fps, ~250–500 kbps up | **safe ceiling** |
| 12 human clients | 30–45 fps, ~0.8–1.2 Mbps up, thermal in ~8 min | **hard ceiling** |
| 16+ human clients | unplayable host, packet loss for everyone | forbidden |
| 50 full AI pawns (skeletal + nav + AI tick) | host dies regardless of humans | **forbidden** |

So: **12 humans is the absolute cap, 8 is the design target.** And 50 real bot pawns is off the table.

### 3.2 Options for reaching "50 players"

| | **A. Honest small match** | **B. 50 real pawns, LOD'd hard** | **C. Hybrid: virtual + physical bots** |
|---|---|---|---|
| Shape | Advertise 12–16 player BR | 10 humans + 40 full AI pawns, aggressive LOD | 10 humans + 40 bots, of which only ~8–12 are *real pawns* at any moment |
| Optimises for | Truth, zero new systems | Faithfulness to the brief | The *feeling* of 50 within the host's budget |
| Costs | Owner's core ask ("50", "Booyah at 50") is dropped; map feels empty; no BR tension | Host CPU and uplink both blow up; needs nav mesh for 40 agents; thermal death at minute 6 | A new subsystem: the **Virtual Combatant Simulator**; bots must be spawned/despawned convincingly |
| Risk | Product risk (feels like a small arena shooter, not a BR) | Technical risk (will not ship on a phone host) | Moderate engineering risk, fully contained server-side on the host |

**Recommendation: C.** Because the only thing a player verifies about the other 38 combatants is the **alive counter, the kill feed, and the minimap** — none of which require a pawn to exist.

### 3.3 Virtual Combatant Simulator (the key idea)

Each of the 40 bot slots is a cheap data record on the host: `{id, name, team, alive, hp, position, state, skill}`. It costs no pawn, no skeletal mesh, no nav query.

| Bot state | Representation | Cost | When |
|---|---|---|---|
| **Virtual** | Data record only. Drifts toward the safe zone on a spline at a plausible speed; participates in a scripted attrition model that produces kill-feed events at FF-like intervals | ~nothing (one array, 2 Hz update) | Default, and whenever no human is within 300 m |
| **Physical** | Real AI pawn with mesh, nav, perception, weapon | ~1 pawn's worth | Promoted when a human enters its 300 m bubble *and* the physical-bot budget has room |
| **Dead** | Record flipped, kill feed emitted, loot bag dropped only if it died physically | ~nothing | — |

Hard budget: **max 10 physical bots alive at once per match** (tunable, device-tiered: 6 on low-end host, 10 on mid, 14 on high). If the budget is full, a far-away physical bot is demoted to virtual (off-screen only, never within 200 m of any human).

Promotion/demotion rules that prevent the illusion breaking:
1. Never promote or demote inside a human's view frustum within 250 m. Use occlusion + distance gates.
2. Promote a virtual bot at the **edge** of the bubble (280–320 m), moving inward, so it visibly "arrives".
3. Virtual-bot positions must be consistent with their minimap/kill-feed story — a bot cannot die to gunfire 800 m from any living combatant.
4. A virtual bot in the final two zones (circle Ø < 200 m) **must** be physical or dead. No ghosts in the endgame; that is where players would notice.

Attrition model (so the alive-count curve matches a real BR): deaths follow the shape players expect — fast drop in the first 90 s (hot-drop deaths), a quiet mid-game, then a sharp fall as the zone squeezes.

| Match time | Alive count target | Source of deaths |
|---|---|---|
| 0:00 (land) | 50 | — |
| 1:30 | ~34 | virtual hot-drop attrition + real human fights |
| 3:00 | ~24 | virtual pairings + real fights |
| 5:00 | ~15 | zone + fights |
| 7:00 | ~8 | mostly physical, zone-driven |
| 9:00 | ≤4 | all physical |

Every virtual death emits a normal kill-feed entry with a plausible killer (another bot, or the zone). Players read the kill feed as the world being alive.

### 3.4 Composition per mode

| Mode | Team size | Nominal slots | Human cap per match | Bot slots | Teams |
|---|---|---|---|---|---|
| Solo | 1 | 50 | **12** | 38 | 50 |
| Duo | 2 | 50 | **12** (6 duos) | 38 (19 duos) | 25 |
| Squad | 4 | 48 | **12** (3 squads) | 36 (9 squads) | 12 |

Humans who queue solo into Duo/Squad are **filled with bot teammates** — a bot teammate is always physical (it is next to you) and counts against the physical budget. This is why Squad costs more host CPU than Solo: 12 humans in 3 squads means up to 4 bot teammates physical at all times. Squad human cap may need to drop to 8 on low-tier hosts.

### 3.5 Matchmaking and bot backfill (server-side)

The queue lives in **Redis**; the backend is the only authority on who goes where. **Matchmaking runs over the existing WebSocket, never HTTP polling** (see §6, ngrok budget).

```
Lobby: player picks Solo/Duo/Squad  ──WS──▶  backend queue:{mode}
                                              │
            ┌─────────────────────────────────┤ match-maker tick (1 Hz)
            │                                 │
     (a) open match exists in           (b) no open match
         JOINABLE window?                     │
            │  yes → assign                    └─▶ create match, elect host (§3.6)
            ▼                                       state = FILLING
   player gets {matchId, hostNetId, team, slot}
            │
            ▼
   client connects to host via EOS P2P  →  Waiting Quad
```

Match lifecycle, server-side:

| State | Meaning | Duration | Exit |
|---|---|---|---|
| `FILLING` | Host elected, Waiting Quad open, humans joining | **≤ 25 s** | human cap reached **or** 25 s timer |
| `BACKFILLED` | Backend stamps the final roster: remaining slots become bots, teams sealed | instant | → `COUNTDOWN` |
| `COUNTDOWN` | 10 s, visible to all, plane path revealed on the map | 10 s | → `IN_FLIGHT` |
| `IN_FLIGHT` | Plane crossing, jump window | ≤ 45 s | last player ejects / path ends |
| `LIVE` | The match | 8–10 min | one team alive |
| `SETTLED` | Results posted to backend | — | — |

**Backfill rule:** after **12 seconds** in `FILLING`, if ≥ 2 humans are present, start a 13-second soft timer and fill the rest with bots. If only 1 human after 25 s, still start — a solo-vs-bots match is better than a dead queue, and this is exactly what FF does for a new account.

**Design decision: no joining a match in progress.** The owner asked for "auto-matchmaking into a *running* match". Dropping a player into minute 6 of a BR is a bad experience in every BR ever shipped (you land with no loot into a 200 m circle). What the owner actually wants is **a short queue**. We deliver that with a 25 s hard cap and bot backfill — the player's perceived wait is identical, and the match is fair. The `FILLING` + Waiting Quad window *is* the "running match" they can walk around in.

### 3.6 Host selection

Backend scores every player in the queue; the highest score becomes host and is told `role = HOST`.

| Signal | Weight | Source | Notes |
|---|---|---|---|
| Device tier (GPU class, RAM, SoC) | 35% | client reports once at login, cached in `devices` table | Bucket into LOW/MID/HIGH; LOW is **never** host if a MID exists |
| Battery level & charging | 20% | client reports at queue time | < 30% and not charging → ineligible |
| Thermal headroom | 10% | Android thermal status | THROTTLING → ineligible |
| Network type | 20% | WIFI / 5G / 4G / 3G | 3G ineligible; WIFI strongly preferred |
| Measured RTT to EOS relay | 15% | client pings relay before queueing | lower is better |
| Recent host reliability | penalty | `host_abandon_rate` over last 20 matches | > 25% abandon → ineligible for 24 h |

Tie-break: lowest relay RTT. If **no** eligible host exists in the queue, the queue holds and the client shows "Finding a host…" — then after 30 s it offers a **Practice vs Bots** offline match (zero infra cost, keeps the player playing). That fallback is worth building early; it is also the single best thing we have for the ngrok budget.

**Host advantage is real and must be disclosed.** Host has 0 ms latency. Mitigations, cheapest first: (1) give the host a visible "HOST" tag so the lobby knows; (2) rotate hosting so the same strong player is not always host — track `times_hosted_today`; (3) do **not** count host matches toward ranked leaderboards in the first season (observe mode is already how `scoring_mode` works today).

### 3.7 Host leaves — what actually happens

**There is no host migration.** Be blunt in the design and then soften the blow with bookkeeping.

| Event | Today (unhandled) | Designed behaviour |
|---|---|---|
| Host closes app / loses signal | All clients hard-disconnect to the lobby, match result lost, no XP, no stats | Clients get a clear screen: **"Host disconnected — match ended"**, and **keep the placement and kills they had earned** |
| Host dies in-game | Host keeps hosting as a spectator (UE listen server does not end on pawn death) | Host is locked into **spectate-until-end** with a "You are hosting — leaving ends the match for 11 players" warning on the quit button |
| Host quits deliberately | same as disconnect | Counts toward `host_abandon_rate`; three abandons in a week → 24 h host ban (not a play ban) |

**The mechanism that saves the rewards: a match-state heartbeat.** The host pushes a compact snapshot to the backend every **20 seconds** over the match WebSocket (1 connection, not 1 request per beat — critical for the ngrok budget): `{matchId, t, aliveCount, [{playerId, kills, damage, alive, placementIfDead}]}`. On abnormal termination the backend settles the match from the last snapshot: everyone dead has their real placement; everyone still alive is credited with the current `aliveCount` as placement. Nobody loses a 9-minute match because someone else's phone rang.

Host-leave frequency is the number to watch. If > 10% of matches die to host abandon, the fix is not host migration — it is **host incentives**: a flat XP bonus for hosting a match to completion, and a "Campus Host" badge. Cheap, and it works.

### 3.8 Replication, relevancy and net culling for 50 actors

Default UE replication evaluates every actor against every connection. With 12 connections and a phone server, we budget explicitly.

| Actor class | Net cull distance | Net update freq | Priority | Notes |
|---|---|---|---|---|
| Human player pawn | **250 m** | 20 Hz near / 8 Hz far (> 120 m) | 3.0 | Distance-based frequency is the single biggest win |
| Physical bot pawn | **200 m** | 12 Hz near / 4 Hz far | 1.5 | Bots do not need human-grade fidelity |
| Virtual bot | **not replicated as an actor** | — | — | Only its minimap/alive-count/kill-feed effects go over the wire, in the GameState |
| Projectile / tracer | 180 m | one-shot multicast, unreliable | 0.5 | Never a replicated moving actor; fire-and-forget cosmetic |
| Loot item on ground | **120 m** | 2 Hz, **dormant until touched** | 0.3 | Dormancy is mandatory — a BR map has thousands |
| Airdrop crate | 600 m (it is a landmark) | 2 Hz | 1.0 | Plus a GameState-replicated map marker |
| Vehicle (see `VEHICLES_DESIGN.md`) | 250 m | 15 Hz driven / dormant parked | 2.0 | Parked = dormant |
| Door (198 on campus) | 80 m | **initial + on-change only** | 0.1 | Already breakable; must be event-driven, not polled |
| Zone state | always relevant | **on phase change only** | — | Circle centre/radius in GameState; clients interpolate locally |
| Plane | always relevant during `IN_FLIGHT` | 5 Hz | 2.0 | Path is a replicated start/end pair; clients lerp |

Non-negotiables:
1. **GameState carries the match truth** — alive count, team count, zone phase, kill feed ring buffer (last 8 entries). One replicated struct, ~200 bytes, is cheaper than 38 actors.
2. **Loot uses dormancy + spatial query, not actor replication.** Clients spawn loot visuals locally from a **seeded deterministic spawn table** (the host sends one `lootSeed` at match start); only *pickups and removals* replicate. This turns thousands of actors into one integer plus a diff list. This is the second-biggest win in the document.
3. **Replication Graph plugin** with a spatial grid (cell size 150 m) — Epic built it for exactly this and it removes the per-actor-per-connection cost. Cost: it does not work with the default actor channel assumptions, so enable it early, not late.
4. Clamp host uplink: `NetServerMaxTickRate 30`, per-connection bandwidth cap ~80 kbps, and measure with `stat net` on the real device.

### 3.9 Performance budget (per frame, mid-tier phone host, 16.6 ms target)

| System | Budget | Enforcement |
|---|---|---|
| Render (host's own view) | 8.0 ms | Mobile renderer, no Lumen/Nanite, 1 dynamic light + baked, 720p–900p dynamic res |
| Replication + net serialize | 2.5 ms | §3.8 culling table, Replication Graph |
| Physical bot AI | **1.5 ms total** (≈0.15 ms × 10) | Tick budget below |
| Virtual combatant sim | **0.1 ms** | 2 Hz, flat array, no actors |
| Human pawn simulation + hit traces | 2.0 ms | Existing lag-comp path |
| Zone, loot, game mode logic | 0.5 ms | Zone updates at 2 Hz, not per frame |
| Audio, UI, misc | 2.0 ms | — |

Bot LOD and tick:

| Bot LOD | Trigger | Tick rate | AI depth | Mesh |
|---|---|---|---|---|
| L0 Combat | human within 60 m, line of sight | 20 Hz | full: perception, cover, aim error model, strafe | full skeletal, animated |
| L1 Alert | human 60–150 m | 10 Hz | move-to, shoot-if-seen, no cover reasoning | skeletal, reduced bone LOD |
| L2 Ambient | human 150–300 m | 4 Hz | path to zone, idle fire | lowest LOD, no cloth/IK |
| L3 Virtual | no human within 300 m | **2 Hz, data only** | spline drift + attrition dice | **none** |

Skeletal mesh rule: **≤ 12 animated characters on screen on the host**, enforced by the physical budget. `URO` (update-rate optimisation) on all non-local characters, animation budget allocator on.

### 3.10 Map size and layout

| Option | Size | Players/km² (nominal 50) | Verdict |
|---|---|---|---|
| Current campus (Classic) | ~0.3×0.3 km | very high | Correct for the 4-round Classic mode. **Too small for BR** — zone phases would be meaningless |
| **A. Compact BR** | **1.2×1.2 km** | 35 | Dense, 6-min matches, lowest streaming cost. Fights constantly. |
| **B. Standard BR** | **1.5×1.5 km** | 22 | FF Bermuda density with our shorter match. Room for vehicles, 8 named POIs, a real plane path. |
| C. Large BR | 2.0×2.0 km | 12 | Feels empty with only 12 humans; parachute-to-first-fight gets long; world streaming on mobile gets risky |

**Recommendation: B, 1.5×1.5 km**, built as **World Partition with 256 m cells** and HLODs. Rationale: 1.2 km gives no mid-game breathing room (BR pacing needs quiet stretches to build tension), and 2.0 km exposes how few humans there are. 1.5 km with a 45-second plane crossing is the sweet spot.

Named POIs — 8 is the right count (FF Bermuda has ~12 on 4 km²; we have 2.25 km²):

| POI | Loot tier | Role |
|---|---|---|
| **Main Block / Admin** | high | Hot drop, central, multi-storey vertical fight |
| **Library** | high | Dense indoor CQB, long sightlines between shelves |
| **Hostel Towers (Boys/Girls)** | mid-high | Two tall buildings, rooftop fights, the map's skybox landmark |
| **Canteen / Mess** | mid | Open-plan, benches as cover, fast loot |
| **Sports Ground / Stadium** | low | Open killing field; crossing it is a commitment |
| **Workshop / Labs** | mid-high | Industrial clutter, vehicle spawn |
| **Bell Tower** | high + unique | Highest point, sniper nest, see §5 |
| **Bus Bay / Parking** | low-mid | Vehicle spawns, road network hub |

Roads connect all eight; the shrinking zone pulls toward a different POI each match.

### 3.11 Zone design (ours)

Five phases over ~9 minutes. Formula, not hand-placed:

```
R(0)   = 700 m                        (first safe circle radius; ~47% of map width)
R(n)   = R(n-1) × 0.58                (each circle ≈ 58% of the previous)
C(n)   = a random point such that the new circle lies entirely inside the previous
         circle, with its centre biased 0.35 toward a named POI
```

| Phase | Starts at | Wait (s) | Shrink (s) | Safe Ø (m) | Shrink rate (m/s) | DPS (% max HP) | Feel |
|---|---|---|---|---|---|---|---|
| 1 | 0:45 | 75 | 60 | 1400 → 812 | ~4.9 | **0.5%** | "get moving", heal-through-able |
| 2 | 3:00 | 45 | 50 | 812 → 471 | ~3.4 | **1.0%** | first rotation pressure |
| 3 | 4:35 | 40 | 45 | 471 → 273 | ~2.2 | **2.5%** | cannot sit outside |
| 4 | 6:00 | 35 | 40 | 273 → 158 | ~1.4 | **5%** | lethal in ~20 s |
| 5 | 7:15 | 30 | 45 | 158 → 40 | ~1.3 | **9%** | final circle, ~8 alive |
| Sudden death | 9:00 | — | 30 | 40 → 0 | 1.3 | **14%** | forces a conclusion |

Damage scales with distance outside: `dps = base_dps × (1 + min(distance_outside / 150, 1.5))`. Being 10 m out in phase 2 is a nuisance; being 200 m out in phase 4 kills in six seconds.

**Dynamic pacing (Sanhok's trick, and we need it more than they do):** if `aliveCount` is below the target curve in §3.3, extend the current wait by up to 20 s; if above, shorten it by up to 15 s. This keeps the match landing at 9 minutes regardless of how the fights went, which matters because our thermal budget is a wall, not a preference.

Zone state is **3 floats + 1 int** in GameState, updated on phase change. Clients interpolate. The damage tick is applied by the host at 1 Hz, not per frame.

### 3.12 Day / night modes

| Mode | Lighting | Gameplay effect | Cost |
|---|---|---|---|
| **Day** (default) | Baked lightmaps + 1 directional | none | baseline |
| **Night** | Second baked lighting scenario, dimmer sky, emissive windows/streetlights | Reduced view distance (fog), **muzzle flashes visible at range**, flashlight attachment, tracers read strongly | One extra lighting scenario bake + one material parameter collection switch |

Implementation stance: **two baked lighting scenarios**, selected at match start from the backend's match config — **not** a real-time day/night cycle. A moving sun on mobile means dynamic shadows for the whole map and it will cost more than every other system in this document combined. Night is a **mode**, chosen per match (e.g. 1 in 4 matches, or an "evening" playlist after 7 PM local — which is also when students actually play, so it doubles as a flavour hook).

---

## 4. Scoring, rewards and "FIRST RANK"

Per-match score (60/40 placement:combat, calibrated against FF's ~19-point Booyah):

| Component | Points |
|---|---|
| Placement #1 | +12 |
| Placement #2–3 | +8 |
| #4–10 | +4 |
| #11–25 | +1 |
| #26–50 | 0 |
| Per kill (human) | +2 |
| Per kill (bot) | **+0.5** |
| Per 500 damage | +1 (cap +3) |
| Survival time ≥ 6 min | +1 |
| Teammate revive | +1 (cap +2) |

**Bot kills must be worth clearly less than human kills.** Otherwise the optimal strategy is to farm bots, and the leaderboard becomes a measure of who got the bottiest lobby. The client should never label a bot as a bot — but the *scoring* must know. (Bellion will want to check that the host cannot forge this; the heartbeat snapshot in §3.7 is host-reported, so the backend must sanity-bound it: kills ≤ 49, damage ≤ kills × 600 + 2000, placement consistent with alive count. Flag outliers into the existing anti-cheat observe mode.)

Win screen: **"FIRST RANK!"** (campus equivalent of Booyah) with the squad line-up, kills, and a shareable result card — the post-match share path already exists in `PLAN.md` §3.3.

---

## 5. The uniqueness angle — why this is a *campus* BR, not a Free Fire clone

The genre mechanics above are table stakes. These are the things only our game can do, and they are mostly **renaming and art direction, not engineering** — the cheapest differentiation available.

| Generic BR | Campus Combat | Cost |
|---|---|---|
| "Blue zone" / "Play zone" | **ATTENDANCE ZONE** — "Attendance is being taken. Get to class." Outside the zone you are "marked absent" and lose HP. Warning line: *"You are absent. -5% per second."* | text + VFX colour |
| Plane / Dornier | **The college bus / fest truck**, or for night mode the **water tanker**. Route is announced over the **PA system** by a "principal" voice | one mesh + audio |
| Random plane line | Path always **passes over the Bell Tower** and the PA announces which POI it crosses first — *"Bus crossing over the Library"* — turning the plane path into a readable, memeable event | 1 constraint in the path generator |
| Airdrop | **EXAM PAPER DROP** — a crate marked with a question-paper icon, announced by the exam bell. Contains the match's only legendary weapon | reskin |
| Zone shrink warning | **THE BELL.** The real college bell rings 30 s before every phase. Five bells = final circle. Audio-only tension, zero GPU cost, instantly understood by every student who hears it | 1 sound cue |
| Zone centre | Biased toward a different campus landmark each match, so the endgame is "final circle on the Canteen roof" — a place players know in real life | already in §3.11 |
| Kill feed | Uses **branch tags** — *"CSE_Shankar eliminated ECE_Rahul"* — feeding directly into the Branch War leaderboard in `GAME_PLAN.md` §3.1 | reuse existing profile data |
| Final-circle flavour | If the final circle lands on the Exam Hall: **"FINAL EXAM"** banner. Rare, memorable, free | 1 condition |
| Night mode | **FEST NIGHT** — stage lights at the ground, DJ audio bleeding across the map, decorated hostel windows | lighting scenario + audio |
| Bots | Named as **juniors / first-years** with plausible roll numbers from the college list. A player killing bots reads it as "ragging the freshers", which is thematically coherent rather than hollow | naming table |

The last row is the strongest idea in this section: **our bot problem becomes our theme.** Nobody is surprised that a campus is full of first-years who cannot shoot.

---

## 6. Backend contract and the ngrok budget

20,000 requests/month is the hardest number in this document. Match flow must be WebSocket-first.

| Interaction | Transport | HTTP requests consumed |
|---|---|---|
| Login / session | HTTP | 1 per session |
| Lobby bootstrap (profile, wallet, friends, boards) | HTTP, **one batched endpoint** | 1 per session |
| Queue → assignment → match state → heartbeats → results | **WebSocket (existing live-events socket)** | **1 upgrade per session** |
| Match settle | over the same WS, host-pushed | 0 |
| Leaderboard view | HTTP, cached in Redis 60 s | ~1 per session |
| **Total** | | **≈ 4–5 per session** |

At 5 requests/session: **~4,000 sessions/month**, ~130/day. That holds the soft launch. The moment we exceed ~30 regular testers, `PLAN.md` already has the answer: Oracle Always Free.

New backend surface needed (contracts to be specified before Igris writes anything):

| Concern | Store | Notes |
|---|---|---|
| `queue:{mode}` | Redis sorted set (score = enqueue time) | TTL 60 s per entry; a dropped socket must self-evict |
| `match:{id}` | Redis hash + Postgres row on settle | State machine from §3.5; Redis is the live truth, Postgres the record |
| `devices` | Postgres | `player_id, soc, ram_mb, gpu_tier, last_thermal, host_score` |
| `host_stats` | Postgres | `times_hosted`, `abandons`, `completed` → feeds §3.6 eligibility |
| Match settle | Postgres, **idempotent on `match_id`** | Host may send the final snapshot twice; a unique constraint on `match_id` plus an `Idempotency-Key` on the settle message is mandatory, or players get double XP on a retry |

Idempotency is not optional here. The host's network is the least reliable component in the system; every settle and every reward credit will be retried.

---

## 7. Phased roadmap

**Tonight-able (MVP-0) — proves the loop end to end, nothing more**

| # | Piece | Why it is tonight-able |
|---|---|---|
| 1 | Waiting Quad level: free movement, no damage, alive counter, 10 s countdown | Reuses the existing match level + HUD |
| 2 | Hardcoded match: **4 humans + 0 bots**, existing campus map | Already works today |
| 3 | **Zone v1**: 3 phases, fixed centres, flat 2% DPS, circle on minimap + full-screen tint when outside | Pure GameState floats + a 1 Hz damage tick |
| 4 | Win condition: last team alive → **FIRST RANK** screen (reuse Player of the Match) | Existing post-match screen |
| 5 | Backend: queue over the existing WebSocket, 25 s fill cap | Socket already exists |

That is a playable BR tonight. No plane, no parachute, no bots, no new map.

**MVP-1 (this week) — it feels like a BR**

| # | Piece |
|---|---|
| 6 | Plane: random chord, 3 s board delay, EJECT button, forced eject at path end |
| 7 | Parachute: freefall 48 m/s, dive 78 m/s, auto-deploy 130 m, steerable, altitude HUD, map landing marker |
| 8 | Zone v2: full 5-phase table from §3.11 + distance-scaled damage + **the bell** |
| 9 | Physical bots, budget 6: nav on the existing campus, L0–L2 LOD, deliberately imperfect aim |
| 10 | Loot v1: deterministic seeded spawn table, 3 tiers, dormant pickups |
| 11 | Host election from the device/battery/network score; `times_hosted` rotation |
| 12 | Match-state heartbeat every 20 s + idempotent settle (so host drops stop costing players their match) |

**MVP-2 (2–3 weeks) — the 50-player illusion**

| # | Piece |
|---|---|
| 13 | **Virtual Combatant Simulator** + attrition curve + promotion/demotion rules (§3.3) |
| 14 | Alive counter reads 50; kill feed populated by virtual deaths; bots named as juniors |
| 15 | Human cap raised 4 → 8 → 12 with a measured device-tier table |
| 16 | Replication Graph + the full culling table (§3.8); `stat net` captured on 3 real devices |
| 17 | Duo/Squad bot teammates, revive, team UI |
| 18 | Exam Paper Drop (airdrop) + legendary tier |

**Phase 3 (1–2 months) — the new map and the mode**

| # | Piece |
|---|---|
| 19 | New **1.5×1.5 km** map, World Partition 256 m cells, HLODs, 8 POIs from §3.10 |
| 20 | Day + **Fest Night** baked lighting scenarios, per-match selection |
| 21 | Vehicles on the BR map (`VEHICLES_DESIGN.md` already specifies them) |
| 22 | BR scoring into the existing weekend board, branch war, rank tiers |
| 23 | Practice vs Bots offline mode (also the no-host-available fallback) |

**Phase 4 (later / conditional)**

| # | Piece | Condition |
|---|---|---|
| 24 | EOS **dedicated servers** (needs a source build of UE 5.8 to cook a server target) | More than ~100 concurrent players, or host abandon > 15% |
| 25 | Host migration | Only meaningful once there is a dedicated-server fallback; otherwise not worth the engineering |
| 26 | Backend to Oracle Always Free | More than ~30 regular testers (already decided in `PLAN.md`) |
| 27 | Ranked tiers with real MMR and bot-ratio-by-tier | Enough matches to have data |

---

## 8. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Phone host cannot carry 12 humans | **High** | Match size collapses | Device-tiered caps (LOW 6 / MID 8 / HIGH 12) measured, not guessed; §9 |
| Host abandons mid-match | **High** | Players lose a 9-min match | Heartbeat settle (§3.7) + host XP bonus + abandon ban |
| Players notice virtual bots | Medium | Trust damage | Endgame rule: no virtual bots inside a 200 m circle; promotion at bubble edge |
| ngrok 20k cap hit mid-fest | Medium | Backend dead during the event we most care about | WebSocket-first (§6); pre-move to Oracle **before** the first fest |
| Thermal throttle at minute 7 | Medium | Host fps collapses in the endgame | 9-min match cap + dynamic zone pacing + host render at 720p |
| EOS relay latency spikes | Medium | Feels bad for everyone | Existing lag comp; show relay RTT in the HUD; prefer WIFI hosts |
| 1.5 km map streaming hitches on mid-tier | Medium | Stutters while parachuting | HLODs, 256 m cells, pre-warm cells along the plane path during countdown |
| Bot kills inflate the leaderboard | **High** if unhandled | Boards become meaningless | Bot kills worth 0.5 (§4) + server-side sanity bounds |

---

## 9. What must be measured before any of this is final

Every number in §3 is an informed estimate. These five measurements turn the estimates into a spec. One evening with three phones answers all of them.

1. **Host capacity curve.** On LOW/MID/HIGH devices: host fps, uplink kbps, and device temperature at 4, 6, 8, 10, 12 connected clients. → fixes the human cap table.
2. **Physical bot cost.** Host fps delta per physical bot at L0/L1/L2. → fixes the physical budget.
3. **Replication cost.** `stat net` with and without the §3.8 culling table and the Replication Graph. → proves the loot-dormancy and distance-frequency wins.
4. **Thermal curve.** Minutes until the host throttles at target load. → confirms or shortens the 9-minute match.
5. **Relay RTT distribution** across the testers' networks. → sets the host-eligibility threshold.

---

## 10. Open questions — the owner's call

1. **Is 12 humans + 38 bots acceptable as "50 players"?** The alive counter, kill feed and minimap will all read 50. The honest alternative is advertising a 12-player BR. (Tusk's recommendation: yes, take the 50 — the whole genre does.)
2. **Does the player ever learn a bot was a bot?** Options: never (FF's approach), post-match only, or a visible tag. Recommendation: never in-match, and bot kills quietly worth less.
3. **Human cap: 8 (safe) or 12 (ambitious)?** Recommendation: ship 8, raise to 12 only after measurement #1.
4. **BR on the existing campus map first, or wait for the 1.5 km map?** Recommendation: ship BR on the existing map at MVP-1 to learn the loop; the big map is Phase 3. The campus map is small for BR, so use a 3-phase zone there.
5. **Night mode: a separate playlist, a random 1-in-4, or time-of-day based (after 7 PM)?** Recommendation: time-of-day — it costs nothing and it is a story.
6. **Do BR matches count toward the weekend leaderboard from day one?** Recommendation: no. Observe mode for one weekend (this is already the established pattern), because host advantage and bot kills will both distort early data.
7. **Match length: 9 minutes, or shorter?** FF ships 10; our thermal budget prefers 8. Recommendation: 9, with dynamic pacing.
8. **If no eligible host is in the queue — Practice vs Bots, or hold the queue?** Recommendation: offer practice after 30 s; it costs zero infrastructure and saves the session.

---

*The Stone of Truth shows the constraint before the code. The constraint here is a single phone, and every number above bends to it. Build sections 1–5 of the roadmap tonight; the 50-player illusion is a system, not a setting, and it earns its own sprint.*

## Sources

- [PUBG Wiki — The Playzone (per-phase tables, Erangel/Miramar/Sanhok)](https://pubg.wiki.gg/wiki/The_Playzone)
- [PUBG Mobile circle prediction & zone damage guide](https://gurugamer.com/mobile-games/pubg-mobile-circle-prediction-guide-10654/amp)
- [PUBG Mobile zone prediction guide](https://gurugamer.com/mobile-games/pubg-mobile-zone-prediction-12047/amp)
- [How does PUBG Mobile matchmaking work (Sportskeeda)](https://www.sportskeeda.com/esports/pubg-how-does-pubg-mobile-matchmaking-work)
- [PUBG Mobile: how bots affect your rank push (Sportskeeda)](https://sportskeeda.com/esports/pubg-mobile-how-bots-affect-your-rank-push)
- [How bots have been implemented in PUBG (pubglookup)](https://pubglookup.com/bots-in-pubg)
- [Free Fire overview — 50 players, ~10 min matches, plane and safe zone (mmos.com)](https://mmos.com/?p=183553)
- [Free Fire parachute guide (gurugamer)](https://gurugamer.com/mobile-games/free-fire-guide-how-to-parachute-effectively-and-get-immediate-advantages-over-your-enemies-10569/amp)
- [Free Fire zone shrink system guide (BlueStacks)](https://www.bluestacks.com/vi/blog/game-guides/free-fire-battlegrounds/ff-zone-shrink-system-guide-vi.html)
- [What happens outside the play zone in Free Fire (Sportskeeda)](https://www.sportskeeda.com/esports/what-happens-outside-play-zone-free-fire)
- [Free Fire OB43 zone timing changes (AFK Gaming)](https://afkgaming.com/mobileesports/news/free-fire-ob43-update-top-changes-coming-from-the-advance-server)
- [Free Fire rank points per match breakdown (gurugamer)](https://gurugamer.com/mobile-games/free-fire-how-to-get-maximum-rank-14393)
- [Free Fire rank guide (BlueStacks)](https://www.bluestacks.com/blog/game-guides/free-fire-battlegrounds/ff-rank-guide-en.html)
- [Unreal Engine — Replication Graph (Fortnite: 100 players, ~50k replicated actors)](https://dev.epicgames.com/documentation/unreal-engine/replication-graph-in-unreal-engine?lang=en-US)
- [UReplicationGraph API reference](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ReplicationGraph/UReplicationGraph)
