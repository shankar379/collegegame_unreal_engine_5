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

## 2. Player flow

```
 LOBBY (phone)
   Pick SOLO / DUO / SQUAD  (Duo/Squad: invite friends -> team)
   Press START
          │  whole team enters the queue together
          ▼
 1. GATEWAY (API servers, many copies, load balanced)
    START -> "put my team in queue SQUAD, region IN"
          ▼
 2. QUEUE  (one per mode × region, e.g. queue:SQUAD:IN)
    Each entry = 1 team (1, 2 or 4 players). Teams are never split.
          ▼
 3. MATCHMAKER (workers; each owns some queues)
    Every 1 s: pull teams -> pack into a session of 50 slots
      SOLO  = 50 teams × 1
      DUO   = 25 teams × 2
      SQUAD = 12 teams × 4 (+2 to reach 50)
    Session FULL (50)          -> step 4
    Waited 25 s, not full      -> fill empty slots with BOTS -> step 4
          ▼
 4. SESSION ALLOCATOR
    Picks a free game-server slot near the players
    (one game-server machine runs many sessions)
    Reply to every player: { session_id, server_ip:port, ticket }
          ▼
 5. BATTLE SESSION  BS-SQUAD-IN-000731
    WAITING AREA: players arrive, run around, no damage, voice on
    All 50 slots ready -> COUNTDOWN 10 s (plane route shown on map)
          ▼
 6. PLANE ✈ ───────────────► flies across the map
    JUMP anytime -> free fall -> parachute -> land
    Loot -> zone shrinks -> fight -> last team = BOOYAH
          ▼
 7. RESULT -> game server reports to API -> rewards / XP
    -> Player of the Match screen -> LOBBY
    Session closed, server slot freed
```

## 3. Session states

| State | What happens | Exit |
|---|---|---|
| `QUEUED` | Team waiting in queue | Placed in a session |
| `FILLING` | Session has a server; players joining the Waiting Area | 50 slots filled **or** 25 s cap |
| `BACKFILLED` | Empty slots become bots, teams sealed | instant |
| `COUNTDOWN` | 10 s, visible to all | `IN_FLIGHT` |
| `IN_FLIGHT` | Plane crossing; players jump | all players out |
| `IN_PROGRESS` | Match running, zone phases | one team left |
| `ENDED` | Results reported, rewards granted | session closed |

## 4. Fill rule

- **DECIDED (recommended): 25 s wait cap, then bot backfill**, then the 10 s countdown. Matches always start fast, as in Free Fire / BGMI.
- **OPEN:** owner may choose a longer cap (e.g. 60 s). One setting: `BR_FILL_CAP_SECONDS`.
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

## 6. Build steps

1. **Rename** BR "room/match" code to **Battle Session** (`battle_session`, IDs `BS-<MODE>-<REGION>-<N>`) in backend (`routes/br.js`, `lib/br.js`, tables) and client (`CampusBR*`).
2. **Queue in Redis per mode × region**; teams enqueue atomically; never split a team.
3. **Matchmaker as its own worker process** (separate from the API), packing teams into 50-slot sessions, 25 s cap, bot backfill.
4. **Session allocator interface** with two implementations:
   - `PhoneHostAllocator` — today: elects a player's phone as host (EOS P2P).
   - `DedicatedServerAllocator` — later: GameLift / Agones.
5. **Result reporting** from the game server, with a 20 s heartbeat so a crashed match still settles rewards (see `BATTLE_ROYALE_DESIGN.md`).
6. **Later (paid):** move matches to dedicated game servers; add regions.

## 7. Related docs

- `BATTLE_ROYALE_DESIGN.md` — map, zone, plane, bots, loot
- `CUSTOM_ROOM_DESIGN.md` — Room Card 4v4 rooms (not Battle Sessions)
- `PLAN.md` — overall status
