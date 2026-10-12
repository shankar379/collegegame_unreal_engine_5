# CUSTOM ROOM DESIGN — Free Fire teardown, our gaps, and what we add

> [TUSK — Stone of Truth]
> I have already seen this system five years out. A room is not a lobby; it is a **contract about rules**
> that one phone enforces and five phones trust. Today our room carries four facts (map, team size,
> rounds, minutes). Free Fire's carries thirty. The foundation must hold before we add the thirtieth.

**Status:** design only. No code in this document. Implementation is Igris's; indexes/locking are Iron's;
the money question (Room Card pricing) is Jinho's call.
**Date:** 2026-10-12
**Scope:** the Custom Room feature only — not matchmade Quick Match, not Battle Royale queueing.

---

## 1. Why this exists

Our custom room today is a *transport* feature: it exists so a friend can reach the host phone
(`code -> {address|puid}`). Free Fire's custom room is a *rules* feature: the code is incidental, the
**settings panel is the product**. That panel is why Free Fire owns Indian college-campus esports — every
scrim, every tournament, every "1v1 me" happens inside a custom room that the host shaped.

If Campus Combat wants to be the game students run their own inter-branch tournaments on, the room has to
carry rules, not just an IP address. This document specifies which rules, in what order, and what the API
and UI must grow to carry them.

---

## 2. Free Fire custom room — the flow as it actually works

### 2.1 Creating a room

| Fact | Detail | Confidence |
|---|---|---|
| Entry point | Lobby -> game-mode selector -> **Custom** tab (bottom right) -> **Create Room** | High |
| Cost (historically) | **1 Custom Room Card per room**, single-use. Bought in Store > Item tab for **100 diamonds**; also from events, guild rewards (Guild Tournament **1,800 dog-tag** milestone), and free giveaways | High |
| Cost (current) | **Room cards were REMOVED.** OB49 patch notes: *"all room cards have been removed. Players can now create custom rooms for free!"* and *"All the original settings are now completely free for players to experiment with and enjoy in custom rooms."* | High (official Garena) |
| Paid tier that replaced it | **Advanced Room Card — 100 diamonds.** Not a gate on creating a room; it is a *prestige + convenience* SKU: room floats to the top of the room list, in-room privilege display + creator effects, a scoreboard that records the previous match's result, and a **cross-team match chat** | High (official Garena) |
| Historical card tiers | Regular Room Card (standard settings) vs **Advance Room Card** (red, introduced OB27) unlocking HP modification, movement speed, weapon restrictions and other advanced knobs. Third-party sources also claim time-limited cards (2 h / 6 h, unlimited rooms) — **unconfirmed, treat as noise** | Medium / Low |
| Room-type picker | **Regular** ("for playing casually with friends") vs **Advance** ("for tournaments or competitive matches") | Medium |
| Anti-spam | **Cannot create another room within 3 minutes** of creating one | High (official Garena) |
| Invite paths | Room ID + password read aloud, **invite link**, **QR code**, friend invite | High |

**Settings panel structure: four tabs — Room, Game, Spectate, Special Settings.** The visible settings
change per mode. This tab split is the single most portable idea in the whole feature and we should copy it.

### 2.2 Modes and maps selectable

- **Modes:** Battle Royale (Classic), Clash Squad, Bomb Squad 5v5, Team Deathmatch, Lone Wolf,
  Rush Hour, Battle Cage. (Pet Rumble / Pet Mania removed in OB49.)
- **BR maps:** Bermuda, Purgatory, Kalahari, Alpine (+ Nexterra in later rotations).
- **Clash Squad maps:** the fixed CS arena set (Bermuda/Purgatory CS variants, Mars Electric etc.).
- **Competitive practice:** organisers lock a small tested map pool — Bermuda and Purgatory are the
  canonical scrim maps.

### 2.3 Max players / teams

| Mode | Players | Teams |
|---|---|---|
| Battle Royale custom | up to **48** (sources differ: 48 vs 50; 48 is the figure that recurs) | 12 squads of 4 (solo / duo / squad) |
| Clash Squad | 4v4 = 8 | 2 |
| Bomb Squad | 5v5 = 10 | 2 |
| Lone Wolf | 1v1 / 2v2 | 2 |
| **Classic mode minimum** | **at least 10 players in the lobby** or the match will not start | — |

### 2.4 The full settings / toggles list

Compiled across guides; labels are approximate where a source paraphrased. Marked **[A]** = Advance-card
tier historically.

**Room tab**
- Room name
- **Password** (room public vs password-protected)
- Room type: Regular / Advance
- **Minimum account level** required to join
- **Block Emulators** (default ON since the Sept 23 update — emulator players cannot join unless the host
  disables it)
- Team mode: Solo / Duo / Squad
- Max player count
- Team lock

**Game tab (BR)**
- Map select
- **Airship type / flight path**
- **Zone speed** (safe-zone shrink speed)
- Zone damage level
- Zone shrink timing
- **Airdrop frequency** (and airdrop on/off)
- **Vehicle spawn rate** (vehicles on/off)
- **Loot drop rate / high-tier loot**
- **Weapon availability + specific weapon bans**
- **Fall damage** on/off
- **EP regeneration**
- **Limited ammo** (yes/no — "unlimited ammo" is the inverse of this toggle; practice rooms set unlimited ON)
- **Character skills ON/OFF**
- **Pet skills ON/OFF**
- **Gun attributes ON/OFF** (weapon attachment/skin stat bonuses)
- **Loadout ON/OFF**
- **[A] HP modification**
- **[A] Movement speed**
- **[A] Jump height**
- **Gloo wall** availability (community guides treat this as a room-level practice knob; also appears as
  unlimited-gloo in training rooms)
- **Auto-revive** (appears in OB49 as a *HUD* option — "limited-time auto-revive" — and in practice guides
  as a room rule for training; the authoritative placement is HUD, so treat room-level auto-revive as
  **unverified**)

**Game tab (Clash Squad)**
- **Rounds: 7 or 13** (first to 4 / first to 7)
- **Starting coins** per player (community default ~1500)
- **Coins awarded per round**
- Starting EP (community default ~50)
- Weapon restrictions
- Character skills / gun attributes on/off (scrims: OFF)

**Spectate tab**
- Allow spectators (yes / organisers only / off)
- **Spectator slots** (how many may watch; no documented maximum)
- **Observer / stream delay** — tournament organisers enable a delay so viewers cannot stream-snipe

**Special Settings tab**
- The mode-specific exotica (per-mode rule overrides). Guides reference it but no source enumerates it
  completely; this is where unlabelled toggles live.

> **Honest limitation of this research:** Garena publishes no canonical settings list. Everything above is
> reconstructed from patch notes plus third-party guides of varying age (50–1900 days). "Unlimited ammo",
> "unlimited gloo walls" and "headshot only" are **community names for training-room configurations**, not
> confirmed first-party menu labels — "headshot only" in particular appears to be a *house rule* enforced
> socially, not a toggle. Do not cite these as Free Fire features; cite them as player demand we can serve
> natively and better.

### 2.5 How players join

1. Host creates the room and shares **Room ID** (numeric) + **password** if set.
2. Joiner: Custom tab -> enter Room ID -> enter password -> lands in the room lobby.
3. Alternatives: the host's **invite link** or **QR code**, or a direct friend invite from the room.
4. Joiner then picks or is placed into a team slot and waits for the host to start.

### 2.6 Host controls

- Move / swap players between teams
- **Kick** a player; kick idle slots
- Lock teams before starting
- **Start** the match (gated on the mode's player minimum)
- Change settings while the lobby is open
- Close the room
- Advance card only: cross-team match chat, previous-match scoreboard in room

### 2.7 Spectator / observer

A first-class role with its own tab and its own slot pool. Spectators occupy spectator slots, not player
slots. Tournament organisers use restricted spectator access + stream delay. This is the mechanism that
makes Free Fire broadcastable at the grassroots level — a casting PC joins as an observer.

### 2.8 Rewards

**Custom rooms award nothing.** No rank/tier movement, no BR/CS ranked points, and custom results do not
feed normal stats. This is the deliberate design that makes the settings safe to open up: when HP, speed
and ammo are host-controlled, no progression may depend on the outcome. The *only* historical cost/reward
loop was the Room Card economy, and OB49 removed even that.

### 2.9 Anti-abuse rules

Garena has no custom-room-specific policy page. The relevant levers in the general **Abuse Policy** are:
- Exploiting errors and bugs instead of reporting them -> "serious penalties".
- Modified clients, packet tampering, hack tools, even cosmetic mods -> permanent ban.
- Deliberately teaming with hackers to push rank -> warning by in-game mail, then a 2-day ban from team
  modes and ranked.
- A credit/behaviour score that blocks ranked access when it falls too low.

The structural anti-abuse measure is the one in §2.8: **zero rewards from custom rooms**, so there is
nothing to farm. Secondary measures: the 3-minute room creation cooldown, minimum account level to join,
and Block Emulators.

### 2.10 Esports / tournament usage

Custom rooms *are* the Free Fire tournament stack from campus scrims to regional qualifiers. The organiser
convention:
- Character skills **OFF**, gun attributes **OFF** — "outcomes depend on aim, movement, positioning and
  game sense".
- **Limited ammo = YES** (no unlimited ammo in competition).
- Locked map pool (Bermuda, Purgatory).
- Standardised Clash Squad starting coins so every team has identical buying power.
- Spectator slots reserved for admins/casters + **observer delay** against stream sniping.
- Room ID + password distributed to captains in a Discord/WhatsApp bracket channel minutes before slot lock.

**Sources**
- Garena official, OB49 patch notes — room cards removed, settings free, Advanced Room Card 100 diamonds, 3-min cooldown, invite link/QR, HUD options: https://ff.garena.com/en/article/1473/
- Garena official, Abuse Policy: https://ffsupport.garena.com/hc/en-us/articles/4412928339866-Abuse-Policy
- BlueStacks custom lobby guide — Custom tab flow, Block Emulators, Classic needs 10 players, 1 card per room, 100 diamonds, 1,800 dog tags: https://www.bluestacks.com/blog/game-guides/free-fire-battlegrounds/ff-custom-lobbies-guide-en.html
- gecktechs — creation flow, four-tab panel, BR map list, BR rule list (limited ammo, character skills, gun attributes, loadouts, airship type, zone speed), CS rounds 7/13 + starting coins + weapon restrictions, spectators, host controls, "custom room results don't affect ranked tier or stats": https://gecktechs.com/gaming-settings/create-custom-room-free-fire/
- freefirenation — Regular vs Advance room type, four tabs, mode list, spectator slots, password, min account level, card tier table: https://freefirenation.com/free-fire-custom-room/
- Sports Digest — tournament-organiser settings: character skills off, limited ammo yes, locked map pool, standardised starting coins, spectator/observer delay: https://sportsdigest.in/free-fire-custom-room-settings-esports/199106/
- Republic World / candid.technology / Sportskeeda — Advance Room Card (OB27, red), 100 diamonds, guild dog-tag route, 48-player figure: https://www.republicworld.com/tech/gaming/how-to-get-a-custom-room-card-in-free-fire , https://candid.technology/how-to-get-room-card-in-free-fire/ , https://sportskeeda.com/free-fire/how-get-free-custom-room-cards-free-fire
- urgugu / vcgamers — community practice-room configuration (unlimited ammo on, gloo wall on, auto-revive off, 4–8 players), CS coin/EP defaults: https://urgugu.com/free-fire-max-custom-room-tricks-in-2026-full-guide/ , https://www.vcgamers.com/news/en/custom-room-ff-clash-squad/
- Sportskeeda — penalties for rank-pushing with hackers: https://www.sportskeeda.com/free-fire/why-free-fire-players-push-rank-hackers

---

## 3. What Campus Combat has today

Read from `C:\UE\Projects\CampusCombat\campuscombat-backend\api\src\routes\rooms.js`,
`migrations/002_rooms.up.sql`, `005_room_rounds.up.sql`, `010_internet_hardening.up.sql`, and game-side
`Source/CollegeCampusGame/Campus/CampusLobbyUI.cpp` (`BuildRooms`) /
`Campus/CampusPlayerControllers.cpp` (`HostRoom`, `JoinRoomByCode`).

**API surface**

```
POST   /v1/rooms                 {map, team_size, max_players?, rounds 4-7, round_minutes, eos_puid?}
                                 costs 1 Room Card, Idempotency-Key, refunded if nobody ever joined
POST   /v1/rooms/join            {code} -> {room, ticket}
GET    /v1/rooms/:code                      members only
GET    /v1/rooms/:code/summary              members only, post-match scoreboard
POST   /v1/rooms/:code/heartbeat            host only, ~30 s
POST   /v1/rooms/:code/admit                host only, {ticket}
POST   /v1/rooms/:code/result               host only, final stats, Idempotency-Key scoped to room
DELETE /v1/rooms/:code                      host only, close (refunding)
```

**Settings the room carries:** `map` (`classic` | `real`), `team_size` (1 | 2 | 4),
`max_players` (2–8, hard-clamped to 8 because a phone hosts the listen server), `rounds` (4–7, **all
played**, tie -> one decider), `round_minutes` (3 | 5 | 7 | 10). That is the entire rule surface.

**Join:** 6-character code from the Crockford-ish alphabet `[A-HJ-NP-Z2-9]`, no password. Membership in
`room_members` is the authorisation; `GET` returns 404 (never 403) to non-members so code-guessing leaks
nothing. Joining mints a short-lived **ticket** which the phone hands the listen server, and the host
calls `/admit` to verify it with the API. Transport is one shape, two kinds:
`{type:'eos',puid}` (relay, internet) or `{type:'lan',address,port}`.

**Economy:** 1 Room Card to create; refunded on close **iff `first_join_at` is null**. Creating a new room
replaces the host's previous live room (unique partial index `rooms_one_live_per_host`).

**Rewards:** unlike Free Fire, **our custom rooms DO pay** — `/result` runs `recordResult`, projects to the
weekend leaderboard, advances missions and scores reward brackets, behind an eligibility gate
(`room_members` count vs `admitted_at` count, `duration_s` cross-checked against `play_started_at` within
20%). *This is the single most important asymmetry in this document — see §5.*

**Host controls:** create, heartbeat, admit, report result, close. **No kick. No team swap. No start
gate. No spectator. No password.**

**Party:** a leader's squad is pulled in by `HandOffRoom`; members receive `party.room_ready` and each
calls `/v1/rooms/join` themselves. API refuses the hand-off with `409 MODE_MISMATCH` if `team_size`
disagrees with party mode.

**Two latent defects found while reading (hand to Igris, not part of this design):**
1. `CampusBackend.cpp:1394` sends `map` as `Map == "RealMap" ? "real" : "classic"`, but the client catalog
   (`CampusMapArt.cpp`) names the 50-player map **`VarsityValley`**, not `RealMap`. Any room created on the
   big map is registered to the server as `classic`. The 50-player map is also unreachable through rooms
   anyway, since `max_players` is clamped to 8.
2. `rooms.status` has no `'lobby'`/`'starting'` state and no server-side notion of "the host pressed START",
   so there is nowhere for a start gate or a settings-lock to live. §6 adds it.

---

## 4. Gap table

Legend — **Add**: build it. **Adapt**: build a Campus Combat equivalent. **N/A**: does not apply to our game.
**Reject**: deliberately not doing it.

| # | Free Fire capability | Campus Combat today | Verdict |
|---|---|---|---|
| 1 | Room card to create (now free; Advanced card 100 diamonds for prestige) | 1 Room Card, refunded if never joined | **Keep** — ours is a scarcity dial on hosting cost (a phone hosts, so hosting is not free for us). Revisit pricing with Jinho, not here |
| 2 | Room name | none (code only) | **Add** — cheap, high perceived value |
| 3 | **Password** | none | **Add — P0.** Our code space is 32^6 ≈ 1.07 B, but rate-limit-bounded guessing plus a public code shared in a class WhatsApp group is a real griefing path |
| 4 | Regular vs Advance room type | none | **Reject** for now. One room type, all settings free. Revisit only if we monetise a prestige tier |
| 5 | Min account level to join | none | **Add — P2.** Our natural analogue is **min level / verified college only** |
| 6 | Block Emulators | none | **Adapt — P3.** Our analogue is "verified build only" (client attestation), which Bellion should scope separately |
| 7 | Creation cooldown (3 min) | none; new room replaces old | **Add — P1**, a 60 s cooldown. Today a host can churn rooms freely and each churn refunds the card |
| 8 | Invite link / QR code | party hand-off + 6-char code | **Add — P2** deep link; QR is a nice-to-have |
| 9 | Mode select in room | **no `mode` column at all** — `map` + `team_size` only | **Add — P1.** Team Clash vs (future) Lone Wolf / Gun Game needs a first-class `mode` |
| 10 | Map select | yes (`classic`/`real`, with the id bug above) | **Fix + extend** |
| 11 | 48-player BR custom | clamped to **8** (phone listen server) | **N/A for rooms** until we have dedicated servers. Document the ceiling honestly in the UI |
| 12 | Solo / Duo / Squad | yes (`team_size` 1/2/4) | have it |
| 13 | Max players | yes (2–8) | have it |
| 14 | Spectator slots + allow-spectators | none | **Add — P1.** This is the single highest-leverage missing feature: it is what lets a branch tournament be *watched* |
| 15 | Observer / stream delay | none | **Add — P3**, only when someone actually streams |
| 16 | Rounds (BR none / CS 7 or 13) | `rounds` 4–7, all played | have it, different shape; keep ours (every round played is a clearer promise) |
| 17 | Round time limit | `round_minutes` 3/5/7/10 | have it |
| 18 | Limited / **unlimited ammo** | none | **Add — P0.** The #1 requested practice toggle |
| 19 | **Unlimited gloo walls** | N/A — we have no gloo wall | **N/A.** Our equivalent, if we ever ship deployable cover, is `unlimited_utility` |
| 20 | **Character skills ON/OFF** | **N/A — Campus Combat has no characters with skills.** We have no character-ability system at all | **N/A — explicitly note this in the UI's absence, and reserve the flag name** so a future ability system inherits a toggle that already exists in the contract |
| 21 | **Pet skills ON/OFF** | N/A — no pets | **N/A** |
| 22 | **Gun attributes ON/OFF** | partial — we have a gun catalog (`catalog_items`, `gun_id` on results) but no stat-modifying attachments | **Reserve — P4.** Add the flag only when attachments exist |
| 23 | **Loadout ON/OFF** | none; loadout exists client-side | **Add — P1** as `loadout: 'player' \| 'fixed'` ("everyone starts with the same gun") — this is what makes a fair 1v1 |
| 24 | Auto-revive | none | **Add — P2** as `auto_revive` (respawn without a teammate), useful for solo practice rooms |
| 25 | **Headshot only** | none | **Add — P1.** Free Fire enforces this socially; we can enforce it in the game mode (`damage_rule: 'normal' \| 'headshot_only'`) and that is a genuine differentiator for aim-training rooms |
| 26 | High-tier loot / loot drop rate | N/A for Team Clash (no floor loot) | **N/A now**, relevant only to a looting mode |
| 27 | Airdrop on/off, frequency | N/A | **N/A** |
| 28 | Vehicle on/off | relevant — see `Docs/VEHICLES_DESIGN.md` | **Add — P2** `vehicles` on/off |
| 29 | Zone speed / damage / shrink | N/A for Team Clash | **N/A** until a BR custom room on dedicated servers |
| 30 | Fall damage on/off | none | **Add — P3** |
| 31 | EP regen / HP mod / move speed / jump height | none | **Add — P3** as a single `fun_modifiers` bundle, gated behind an "unranked" flag (§5) |
| 32 | Weapon bans | none | **Add — P2** `banned_guns[]` validated against `catalog_items` |
| 33 | CS starting coins / per-round coins | N/A — no in-match economy | **N/A.** We have no buy phase |
| 34 | Join by Room ID + password | join by 6-char code, no password | **Extend** with password |
| 35 | **Kick** | none | **Add — P0.** Without it a griefer in a 4-player room cannot be removed |
| 36 | **Team swap / move players** | none — teams are assigned by the match server | **Add — P1.** Server-side `room_members.team` the host can set, which the match reads at admit |
| 37 | Team lock | none | **Add — P1**, folds into the start gate |
| 38 | **Start** (host presses start, gated on minimum) | implicit — the host opens the level immediately on create; joiners travel on join | **Add — P0.** This is an architectural change, not a toggle: we need a **lobby state** before `playing` (§6) |
| 39 | Change settings while lobby open | none (settings are fixed at create) | **Add — P1.** `PATCH /v1/rooms/:code` while `status='lobby'` |
| 40 | Previous-match scoreboard in room | `GET /:code/summary` exists post-match | partially have it |
| 41 | Cross-team match chat | none | **P4** — out of scope; Gunhee must review any chat before it ships |
| 42 | **No rewards from custom rooms** | **we DO reward custom rooms** | **See §5 — the load-bearing decision** |
| 43 | Creation / abuse policy | eligibility gate + 20% duration check + ledger | stronger than Free Fire's; **extend it to cover settings** (§5) |

---

## 5. The load-bearing decision: rewards vs settings

This is the one thing in this document that needs the user's call, and everything in §7 depends on it.

Free Fire can open up HP, speed and unlimited ammo precisely *because* custom rooms pay nothing. We pay
leaderboard points, missions and reward brackets out of `/v1/rooms/:code/result`. The moment a host can set
`unlimited_ammo`, `headshot_only=false`, `auto_revive`, 7 rounds of 10 minutes and a fixed easy loadout,
**our reward path becomes a farm** — and the existing eligibility gate does not see it, because the gate
checks *who was admitted and for how long*, never *under what rules*.

**Options**

- **A) Ranked-safe allowlist.** Server computes `is_ranked` from the settings. A fixed allowlist of
  settings (map, team_size, rounds, round_minutes, password, spectators, kick, team swap, loadout=player,
  headshot_only) keeps a room ranked. Anything outside it (unlimited ammo, auto-revive, fun modifiers,
  weapon bans, fixed loadout) flips `is_ranked=false` and `/result` records the match for the scoreboard
  and the player's own history but **awards nothing** — no points, no missions, no brackets.
  *Optimises for:* the reward economy's integrity, and it lets us open every fun toggle on day one.
  *Costs:* two classes of room to explain in the UI; players will be annoyed the first time their
  unlimited-ammo match pays nothing.

- **B) Two room products.** "Ranked Room" with a locked settings set, and "Practice Room" with everything
  unlocked and zero rewards. Same enforcement, different packaging.
  *Optimises for:* comprehension — the player chooses the trade before they build the room, not after.
  *Costs:* two creation flows; a host who wants one fun toggle must abandon rewards wholesale.

- **C) Reward-neutral settings only.** Ship only settings that cannot distort outcomes (password,
  spectators, kick, team swap, start gate, room name, rounds) and refuse the fun toggles entirely.
  *Optimises for:* zero economy risk, smallest build.
  *Costs:* we never get the practice/scrim use case, which is exactly why Free Fire's rooms won campuses.

**Recommendation: A, presented as B.** One room creation flow with a live **"Ranked ✓ / Unranked"** chip
that flips the instant a setting leaves the allowlist and names the setting that flipped it — because the
truth is a server-computed predicate on the settings (A), and the thing a player needs is a product-level
promise they can read before they commit (B). `is_ranked` is computed **server-side only, on the stored
settings row**, never trusted from the client.

---

## 6. The architectural prerequisite: a lobby state

Three P0 items (Start, Kick, Team swap) and one P1 (edit settings while open) are all blocked on the same
missing concept. Today `HostRoom` creates the room and **immediately** `OpenLevel`s into the match; each
joiner `ClientTravel`s the moment `/join` returns. There is no moment at which everyone is present and
nobody is playing — so there is nothing to kick from, nothing to swap inside, and nothing to start.

**Required:** `rooms.status` becomes `'lobby' | 'open' | 'playing' | 'closed'`, where:
- `lobby` — host has created the room, the host's phone is on the **room screen**, not in the level.
  Settings are mutable. Members may be kicked and moved. `is_ranked` is recomputed on every change.
- `open` — host pressed START: the listen server is up, settings are **frozen** (the settings hash is
  stamped onto the room row), joiners travel. Preserves today's exact behaviour for old clients.
- `playing`, `closed` — unchanged.

Old clients that never send `status='lobby'` keep working: a room created without the new
`?lobby=true` flag is born `open`, exactly as today. That is what makes this shippable without a
flag-day client update.

---

## 7. Prioritised plan — what WE add

### P0 — ship first (safety and basic host authority)

| Item | Why |
|---|---|
| **Password** | A code shared in a class group is a public code. Host sets 4–8 digits; `/join` requires it |
| **Kick** | Host must be able to remove a griefer mid-lobby and have the match server drop them |
| **Lobby state + START gate** (§6) | Unblocks kick, team swap, settings edit. The one architectural change |
| **`unlimited_ammo`** | Highest-demand practice toggle; trivial in the game mode |
| **`is_ranked` computation + the chip** (§5) | Must land *with* the first non-neutral toggle, never after it |

### P1 — the scrim room

| Item | Why |
|---|---|
| **Spectator slots** (`spectators` 0–4, separate from `max_players`) | Makes a branch tournament watchable. Needs a spectator ticket scope and a `room_members.role` |
| **Team swap** (`room_members.team`, host-set, read at `/admit`) | Fair teams are the #1 scrim complaint; today the match server decides |
| **Team lock** | Folds into START |
| **`headshot_only`** | Our differentiator. Free Fire enforces it socially; we enforce it in the mode |
| **`loadout: 'player' \| 'fixed'` (+ `fixed_gun_id`)** | What makes a 1v1 fair |
| **`mode` column** | Rooms currently cannot express a mode. Everything after this depends on it |
| **`PATCH /v1/rooms/:code`** while `status='lobby'` | Host changes their mind without burning a Room Card |
| **Room name** | Cheap, and it is what a tournament bracket refers to |
| **Creation cooldown (60 s)** | Closes the churn-and-refund loop |
| **Map id fix** (`VarsityValley` vs `RealMap`, §3) | A correctness bug, not a feature |

### P2 — the fun room

`auto_revive` · `vehicles` on/off · `banned_guns[]` · invite deep link · min level / verified-college-only
join · spectator role in the post-match summary

### P3 — polish

`fall_damage` · `fun_modifiers` bundle (HP / move speed / jump) · observer stream delay · QR invite ·
verified-build-only join (scope with Bellion)

### P4 — reserved, do not build yet

`character_skills` (**N/A — Campus Combat has no character ability system; reserve the flag name so a
future system inherits the contract**) · `pet_skills` (N/A, no pets) · `gun_attributes` (no attachments
yet) · cross-team match chat (Gunhee review required) · BR custom rooms >8 players (needs dedicated
servers, not phone hosting)

---

## 8. API changes needed

No code here — the shape of the contract, for Igris to implement and Iron to index.

### 8.1 One settings object, server-authoritative

Every new rule lives in a single `settings jsonb` column on `rooms`, **not** in a new column per toggle.
Rationale: toggles will churn weekly during tuning and a column per toggle means a migration per toggle;
the settings blob is never queried by predicate (only read whole, by code), so jsonb costs us nothing in
index terms. The *existing* five fields (`map`, `team_size`, `max_players`, `rounds`, `round_minutes`)
**stay as columns** — they are constrained, they are in old clients' request and response shapes, and
moving them would break the wire format for no gain.

Server validates the blob against a Zod schema with an explicit **allowlist of keys**; unknown keys are
rejected (not stripped), so a client sending a toggle the server does not know about fails loudly instead
of silently playing by different rules than it displayed.

### 8.2 `POST /v1/rooms` — additive

Adds, all optional, all defaulted to today's behaviour:
`name`, `password`, `mode`, `spectators` (0–4), `lobby` (bool — create in `lobby` state vs today's
immediate `open`), and `settings` (the blob: `unlimited_ammo`, `headshot_only`, `loadout`, `fixed_gun_id`,
`auto_revive`, `vehicles`, `fall_damage`, `banned_guns[]`, `min_level`, `verified_college_only`).

Response gains `is_ranked`, `unranked_reasons[]` (the named settings that cost ranked status), `settings`,
`name`, `mode`, `spectators`, `has_password`. **Never** returns the password or its hash.

Keeps: 1 Room Card, `Idempotency-Key`, the existing rate limit. The idempotency key must cover the
settings — a retry with different settings is a different request and must 409, not replay.

### 8.3 `POST /v1/rooms/join` — additive, with a new failure mode

Accepts `{code, password?, as: 'player' | 'spectator'}`.

New errors: `ROOM_PASSWORD_REQUIRED` (403), `ROOM_PASSWORD_WRONG` (403, **separately rate-limited per
code — 5 attempts per code per 10 minutes, and the attempt counter lives on the room, not the caller**, so
one attacker cannot be laundered through many accounts), `ROOM_LEVEL_TOO_LOW` (403),
`ROOM_SPECTATORS_FULL` (409), `ROOM_NOT_STARTED` (409 — joined a `lobby` room, wait for START),
`ROOM_KICKED` (403, and this one must be sticky: see 8.5).

Spectators consume `spectators`, never `max_players`. The ticket gains a `role` claim so the match server
can admit a spectator into observer mode and nothing else.

### 8.4 `PATCH /v1/rooms/:code` — new, host only, `status='lobby'` only

Body: any subset of `{name, password, settings, spectators, max_players, rounds, round_minutes, map,
team_size}`. Returns the full room plus the recomputed `is_ranked` / `unranked_reasons`.
`409 ROOM_ALREADY_STARTED` once `status <> 'lobby'`. Optimistic concurrency via a `settings_version`
integer the client echoes back, so two hosts on two devices cannot silently clobber each other.

### 8.5 `POST /v1/rooms/:code/kick` — new, host only

Body `{player_id}`. Removes the `room_members` row **and writes a `room_kicks(room_code, player_id,
kicked_at)` row**, because deleting the membership alone lets the kicked player re-`/join` instantly with
the code they still have. `/join` checks `room_kicks` first and returns `ROOM_KICKED`. The host cannot kick
themselves. Must also invalidate any outstanding ticket for that player — the simplest correct answer is a
`room_members.ticket_epoch` the ticket carries and `/admit` compares.

### 8.6 `POST /v1/rooms/:code/teams` — new, host only, `status='lobby'` only

Body `{assignments: [{player_id, team}]}`, validated against `team_size` and `max_players` so a host
cannot build a 4v1. The match server reads the assignment at `/admit` (which already returns a player
identity — it gains `team` and `role`).

### 8.7 `POST /v1/rooms/:code/start` — new, host only

Transitions `lobby -> open`, **freezes the settings** (stamps `settings_hash` and `settings_frozen_at`),
and returns the travel options string the host's client uses to `OpenLevel`. Enforces a per-mode player
minimum (2 for Team Clash). Members are notified through the existing social channel with a
`room.started` event, reusing exactly the `party.room_ready` path that already works — no new push
mechanism.

### 8.8 `POST /v1/rooms/:code/result` — one field, one rule

The host reports as today. The server **ignores any claim about rules from the client** and reads
`is_ranked` and `settings_hash` from the room row it already has. When `is_ranked = false`,
`recordResult` records the scoreboard and the player's own match history and **skips** points, mission
progress, board projection and reward brackets. The response gains `ranked: false` and a reason so the
post-match screen can say *"Practice match — unlimited ammo was on, so no points."*

### 8.9 Migration `017_room_settings`

Forward: add `rooms.name text`, `rooms.mode text NOT NULL DEFAULT 'team_clash'`,
`rooms.password_hash text`, `rooms.spectators int NOT NULL DEFAULT 0 CHECK (spectators BETWEEN 0 AND 4)`,
`rooms.settings jsonb NOT NULL DEFAULT '{}'::jsonb`, `rooms.settings_version int NOT NULL DEFAULT 1`,
`rooms.settings_hash text`, `rooms.settings_frozen_at timestamptz`,
`rooms.is_ranked boolean NOT NULL DEFAULT true`;
relax the `status` CHECK to include `'lobby'`;
add `room_members.team smallint`, `room_members.role text NOT NULL DEFAULT 'player' CHECK (role IN
('player','spectator'))`, `room_members.ticket_epoch int NOT NULL DEFAULT 1`;
create `room_kicks (room_code, player_id, kicked_at)` with PK `(room_code, player_id)` and
`ON DELETE CASCADE` from `rooms`.

Every column is nullable or defaulted, and `'lobby'` is additive — **the old API continues to pass its own
tests unchanged**, which is the test that this migration is safe.

Back: drop the new columns and the table, and restore the narrower `status` CHECK. The CHECK restore is the
only destructive step, so it runs in **two phases**: deploy the relaxed CHECK first, and on rollback close
any `status='lobby'` rooms (refunding, via the existing `refundingClose`) **before** tightening the CHECK.
Iron reviews the `status` CHECK swap for lock duration — `ALTER TABLE ... DROP CONSTRAINT / ADD CONSTRAINT
NOT VALID` then `VALIDATE CONSTRAINT`, so the write path is never blocked on a full table scan.

---

## 9. UI changes needed

Game-side, `Source/CollegeCampusGame/Campus/`.

**`BuildRooms()` splits in two.** It is currently a single `Dialog` with Quick Match, the rounds/time row,
Create, and the join field. That dialog stays as the **entry point** (Quick Match, JOIN by code, and
"Create Custom Room" now opening the new screen rather than creating immediately).

**New screen: `EScreen::RoomSetup`** — the Free Fire four-tab idea, adapted to three tabs we actually have
content for:
- **ROOM** — room name, password, max players, spectator slots, team mode (solo/duo/squad), min level /
  verified-college-only
- **MATCH** — map, mode, the existing rounds + round-time chips, loadout (player vs fixed + gun picker),
  banned guns
- **RULES** — `unlimited_ammo`, `headshot_only`, `auto_revive`, `vehicles`, `fall_damage`

A persistent footer on every tab: the **RANKED / UNRANKED chip** (server-returned, never computed on the
client) naming the setting that flipped it, the Room Card cost and balance, and CREATE.

**New screen: `EScreen::RoomLobby`** — the thing we have never had. Team columns with every member, drag or
tap-to-move between teams (host only), kick button per member (host only), spectator list, the room code
and password with a COPY and a SHARE, an EDIT SETTINGS button back into RoomSetup, and **START** (host
only, disabled with the reason when below the minimum). Non-hosts see the same screen read-only with
"Waiting for the host to start".

**`HostRoom()` changes shape.** It currently creates the room and `OpenLevel`s in one breath. It becomes:
create in `lobby` state -> show `RoomLobby` -> on START, call `/start` and *then* `OpenLevel` with the
options the server returned. The party hand-off moves from create-time to start-time, which is strictly
better: the squad is pulled in while everyone can still see the room screen.

**`JoinRoomByCode()` grows a password prompt** and must handle `ROOM_NOT_STARTED` by landing the joiner in
`RoomLobby` instead of travelling — that is the whole point of the lobby.

**Match side:** `ACampusMatchGameMode::AdmitWithTicket` already calls `/admit`; it now also reads `team`
and `role` from the response, puts spectators into an observer pawn, and applies the frozen `settings`
(which travel on the URL options string the way `Rounds` and `Minutes` already do) — with the server's
room row as the authority the host's client cannot overrule.

---

## 10. Open questions — the user's call

1. **§5 is the big one.** Ranked-safe allowlist with an UNRANKED chip (recommended), two separate room
   products, or refuse the fun toggles entirely?
2. **Room Card economy.** Free Fire deleted the gate and sold prestige instead. Do we keep 1 card per
   room? Our hosting cost is real (a student's phone and data), which argues for keeping it — but it also
   throttles exactly the scrim usage we are trying to grow. Jinho should price this.
3. **Spectator slots.** Does a phone listen server have the headroom for 8 players + 2 observers? That is
   Baruka's measurement to make, and it caps `spectators` before the API constrains it.
4. **Does `headshot_only` fit the gunplay we have?** It is our strongest differentiator on this list, but
   it is only fun if our hitboxes and netcode make headshots feel earned rather than random. Needs a
   playtest before it earns a P1.
5. **Password length and shape.** 4 digits is what a student will actually read aloud across a classroom;
   6 alphanumeric is what survives a determined guesser. The per-code attempt limit in §8.3 makes 4 digits
   defensible — confirm that is the trade you want.
6. **Do we want public rooms at all** (a browsable room list, as Free Fire has), or is code-and-password
   the whole model? A public list is a moderation surface and Gunhee would need to review it.

---

*The Stone of Truth shows the constraint before the code: our constraint is not the settings panel — it is
that our custom rooms pay out and Free Fire's do not. Settle §5 and everything else here is carpentry.*
