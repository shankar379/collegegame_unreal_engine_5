# Battle Session — 50-player Battle Royale flow

Status: plan, approved for design. Owner decisions marked **DECIDED** / **OPEN**.

## 1. Names

| Term | Meaning |
|---|---|
| **Room** | Custom 4v4 game created with a **Room Card**. Unchanged. |
| **Battle Session** | The 50-player battle royale instance. Code name `battle_session`. |
| **Session ID** | `BS-<MODE>-<REGION>-<NUMBER>`, e.g. `BS-SQUAD-IN-000731` |
| **Queue** | Where teams wait to be placed into a Battle Session (one per mode × region) |
| **Waiting Area** | In-game place where session players gather before the countdown (no damage) |

Never call a Battle Session a "room" in code, UI or docs.

## 2. Player flow — the PUBG / Free Fire model

The player **never waits in the lobby for a full match.** They are placed into an OPEN session in
1–2 s and walk straight into the Waiting Area **while it is still filling.**

```
 LOBBY (phone)
   Pick SOLO / DUO / SQUAD  (Duo/Squad: invite friends -> team)
   Press START                       -> "SEARCHING FOR SESSION..."  [CANCEL]
          │  whole team, one ticket, the LEADER posts it
          ▼
 1. POST /v1/br/queue  {mode, region, eos_puid?, day_night?}
    The reply ITSELF usually carries the session:
      battle_session != null  -> placed. TRAVEL NOW (state FILLING).
      battle_session == null  -> keep searching; the session arrives on
                                 `bs.placed` (or the 5 s fallback read).
          ▼
 2. PLACEMENT  = an OPEN session, or a new one
    connect.type "eos"       -> the first placed player's phone is the listen-server HOST
    connect.type "dedicated" -> address:port, nobody hosts (the AWS path)
    HOST: beats /heartbeat IMMEDIATELY, FROM THE LOBBY (see §4), then opens the
          level as a listen server with ?Session=<id>?Fill=<fill_s>
    MEMBERS: travel to the host's EOS puid, or to the dedicated address
          ▼
 3. WAITING AREA  (state FILLING)        no damage, free movement
    Every arrival spawns on a RANDOM FREE SPOT of the platform
    HUD: PLAYERS 7 / 50 · WAITING FOR PLAYERS · BS-SQUAD-IN-000731
    More players keep arriving while this is on screen
          ▼
 4. THE BACKEND SEALS IT  (`bs.sealed`, reason full | fill | timeout)
    Full (50), or the fill cap ran out -> empty slots become BOTS
    Delivered to everyone; the HOST calls ACampusBRGameMode::SealSession
          ▼
 5. COUNTDOWN 10 s        "MATCH STARTING 10..."   joins are refused from here
          ▼
 6. PLANE ✈ ───────────────► flies across the map
    JUMP anytime -> free fall -> parachute -> land
    Loot -> zone shrinks -> fight -> last team = BOOYAH
          ▼
 7. RESULT -> host reports to the API -> rewards / XP
    -> Player of the Match screen -> LOBBY
    Session closed, server slot freed
```

## 3. Session states

| State | What happens | Exit |
|---|---|---|
| `QUEUED` | Team waiting in queue (often skipped: the POST places them) | Placed in a session |
| `FILLING` | Session has a host; players are **in the Waiting Area** and still arriving. `sealed: false` — this is the only state `DELETE /v1/br/queue` can cancel | Full **or** the fill cap |
| `BACKFILLED` | Empty slots became bots, teams sealed | instant |
| `COUNTDOWN` | 10 s, visible to all. `sealed: true`; joins refused | `IN_FLIGHT` |
| `IN_FLIGHT` | Plane crossing; players jump | all players out |
| `IN_PROGRESS` | Match running, zone phases | one team left |
| `ENDED` | Results reported, rewards granted | session closed |

## 4. The seal, and who owns what

**THE BACKEND DECIDES THE SEAL. The client never starts a countdown on its own clock.**

| Decision | Owner |
|---|---|
| Placement (which session, who hosts) | backend |
| When joins close (full, or fill cap → bots) | **backend** |
| How many bots | backend (`bots`) |
| Countdown length | backend (`countdown_s` / `starts_at`), default 10 s |
| Where a player spawns on the platform | client |
| Who is actually present at the seal | **client** — a placed player who never arrived is not counted; bots fill the rest |

**The seal reaches the host two ways, and both are implemented:**

1. `bs.sealed` on the socket — `{session_id, humans, teams, bots, total_slots, countdown_s, starts_at, reason}`.
2. The **heartbeat response** — the beat itself can seal the session, so `{sealed: true, humans, bots, countdown_s, starts_at}` comes back on `POST /v1/br/:handle/heartbeat`.

Both land in `UCampusBRQueue::TakeSeal`, which parses defensively, ignores a seal for a session this
phone is not in, and broadcasts `OnSessionSealed(Humans, Bots, CountdownS)` **once per session**. On
the host that calls `ACampusBRGameMode::SealSession`, which is idempotent.

**SAFETY FALLBACK (client):** if no seal arrives within `fill_s + 15 s` of the level starting, the game
mode seals locally. A lost message must never strand a player on the platform — and it is what makes
the solo tester's run work: waiting → local seal → 49 bots → countdown → plane.

**THE HOST'S BEAT.** The server deletes a session nobody has beaten within **10 s** as empty, which is
less than a level load — so the host starts beating **from the lobby**, the instant the placement names
it as host. Every **5 s** while `FILLING`, every **20 s** after the seal. A host **never** announces
`COUNTDOWN` (400: the seal is the server's); it only announces `IN_FLIGHT` and `IN_PROGRESS`.

**THE WAITING AREA.** A 40 m slab at the map corner, 40 m up, no damage. Spawning is a jittered 8 × 7
grid of 56 cells: cells are tried in a **random order** and one with a pawn already inside **1.5 m** is
skipped, so fifty arrivals spread out instead of stacking on whoever came first
(`ACampusBRWaitingArea::FreeSlot`).

**CANCEL.** `DELETE /v1/br/queue` works from the queue *and* from a `FILLING` session — the leader
cancelling closes it and requeues everyone else (`bs.ended` with `requeued: true`, which keeps them on
the SEARCHING screen). After the seal it is refused with `409 BS_SESSION_STARTED`, so the button is
gone by then.

### Fill rule

- **DECIDED: 25 s fill cap, then bot backfill**, then the 10 s countdown. Matches always start fast, as in Free Fire / BGMI.
- **OPEN:** owner may choose a longer cap (e.g. 60 s). One setting: `BR_FILL_CAP_SECONDS`, reported to the client as `fill_s`.
- Waiting for 50 real players with no cap is rejected: with few players online the queue would never start.

## 5. Scaling to very large player counts

Each queue (mode × region) is independent, so capacity grows by adding matchmaker workers and game-server machines. Nothing waits on one central server.

| Stage | Online players | What is needed | Rough cost |
|---|---|---|---|
| **Now (testing)** | ≤ 50 | PC + ngrok; one phone hosts the match (listen server over EOS P2P) | ₹0 |
| **Launch** | 1k–10k | Cloud API (Oracle/AWS), Redis queues, **dedicated game servers** (AWS GameLift or Agones on Kubernetes) | ₹ thousands / month |
| **Big** | 100k+ | Several regions (IN, SEA, …), auto-scaling server fleets, matchmaker sharded by region × mode | ₹ lakhs / month |
| **100M registered** | ~1–3M at peak | Global regions, tens of thousands of game servers, ops team | crores / month |

The current phone-hosted + PC/ngrok setup **cannot** scale past testing; it is for development only.

## 6. Client map (where each part lives)

| Part | File |
|---|---|
| Queue, placement, seal, cancel | `Campus/CampusBR.cpp` — `UCampusBRQueue` |
| Host heartbeat (lobby → match) | `Campus/CampusBR.cpp` — `UCampusBRHostBeat` |
| Socket events (`bs.placed`, `bs.sealed`, `bs.left`, `bs.ended`) | `Campus/CampusSocial.cpp` |
| Travel (host opens listen / member dials) | `Campus/CampusPlayerControllers.cpp` — `JoinBRMatch` |
| SEARCHING screen, cancel, travel retry | `Campus/CampusBRUI.cpp` |
| The seal seam, bots, countdown, join refusal | `Campus/CampusBRGameMode.cpp` — `SealSession` |
| Waiting platform and its random spawn | `Campus/CampusBRPlane.cpp` — `ACampusBRWaitingArea` |
| In-match waiting HUD (PLAYERS x / 50, session id) | `Campus/CampusBRHud.cpp` |

## 7. Build steps

1. **Rename** BR "room/match" code to **Battle Session** (`battle_session`, IDs `BS-<MODE>-<REGION>-<N>`) in backend (`routes/br.js`, `lib/br.js`, tables) and client (`CampusBR*`).
2. **Queue in Redis per mode × region**; teams enqueue atomically; never split a team.
3. **Matchmaker as its own worker process** (separate from the API), packing teams into 50-slot sessions, 25 s cap, bot backfill.
4. **Session allocator interface** with two implementations:
   - `PhoneHostAllocator` — today: elects a player's phone as host (EOS P2P).
   - `DedicatedServerAllocator` — later: GameLift / Agones.
5. **Result reporting** from the game server, with a 20 s heartbeat so a crashed match still settles rewards (see `BATTLE_ROYALE_DESIGN.md`).
6. **Later (paid):** move matches to dedicated game servers; add regions.

## 8. Related docs

- `BATTLE_ROYALE_DESIGN.md` — map, zone, plane, bots, loot
- `CUSTOM_ROOM_DESIGN.md` — Room Card 4v4 rooms (not Battle Sessions)
- `PLAN.md` — overall status
