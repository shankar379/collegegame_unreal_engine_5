# Gun Attributes & Purchased-Skin Modifiers — Design

> [TUSK — Stone of Truth]
> I have already seen this system five years out. The foundation must hold today: the moment a
> purchased gun is stronger than a looted gun, every balance complaint for the life of this game
> becomes a monetisation complaint. The cap is the design.

**Status:** design, approved for implementation. No code in this document.
**Owners:** schema/contract — this doc · gun table — `Source/CollegeCampusGame/Campus/CampusWeapons.{h,cpp}` ·
store/catalogue — `campuscombat-backend/api/src/routes/store.js`, `migrations/004_store.up.sql` ·
room handoff — `campuscombat-backend/api/src/routes/rooms.js`

---

## 1. Why this exists

Campus Combat sells guns. Two questions had no answer:

1. **What does a bought gun actually do** that a looted gun does not?
2. **Who decides** — the phone that bought it, or the server?

Answer in one line: **every gun has one BASE stat row that everyone gets from ground loot; owning
and upgrading that gun applies a small, capped, server-computed modifier on top (2 positives + 1
negative, Free Fire style, max ±20% per attribute); and the host learns a player's gun from the
backend at admit time, never from the client.**

---

## 2. Prior art: how Garena Free Fire actually does it

Researched 2026-10-12. Sources are community guides (Gurugamer, Sportskeeda, TechGenyz); Garena
publishes no numeric table, so these are directionally reliable and numerically approximate.

### 2.1 The eight attributes and the +/− bar

Free Fire's gun info screen shows eight attributes as **bars**, not numbers:

`Damage · Rate of Fire · Range · Reload Speed · Magazine · Accuracy · Movement Speed · Armor Penetration`

A skin does not show "+7%". It shows **`+`**, **`++`**, or **`−`** against specific attributes. The
numbers are deliberately hidden; players read it as a shape, not a spreadsheet. Published absolute
attribute values (e.g. M1887 damage 100 / armor-pen 28, Woodpecker damage 85 / armor-pen 77, M82B
armor-pen 54, M249 magazine 100) are 0–100 index scores, not engine units — and community sources
disagree with each other (MP40 accuracy is quoted as 17, 27 and 37 in three different guides).

### 2.2 Rarity tiers and what each tier grants

| Tier | Stat effect | Non-stat |
|---|---|---|
| **Common / "no attribute"** | none — pure cosmetic | look only |
| **Theme** | "slight boost", typically one `+` | look only |
| **Legendary** | "decent boost", typically 2 positives + 1 negative | kill-feed, animation, show-in-lobby |
| **Incubator** | best in class; community calls the top ones "overpowered" | all Legendary features |
| **Evo (evolution)** | levels 1→7, boost grows with level | effects, emote, gloo-wall bonus |

### 2.3 The shape every stat-bearing skin has: 2 up, 1 down

Observed consistently:

| Skin | Shape |
|---|---|
| SCAR Megalodon Alpha | Damage `+`, Rate of Fire `++`, Reload Speed `−` |
| AK Flaming Dragon | Damage `++`, Rate of Fire `+`, Magazine `−` |
| XM8 Aurous Dragon | Range `+`, Accuracy `+`, Rate of Fire `−` |
| UMP Booyah Day (Evo, max) | Damage `++`, Fire Rate `+`, Reload `−` |
| M4A1 Infernal Draco (Evo, max) | Damage `++`, Fire Rate `+`, Reload `−−` |
| MP5 Platinum Divinity (Evo) | L1: Damage `+`, Fire Rate `+` → L7: Damage `++`, Fire Rate `+`, Reload `−` |

**Pattern worth stealing:** the positives are almost always **Damage and/or Fire Rate**; the negative
is almost always **Reload, Magazine, or Range** — i.e. the penalty lands *outside* the duel, on
sustain, not on the trigger pull. Accuracy and Movement Speed are rarely touched. Armor Penetration
appears only on marksman/sniper classes.

### 2.4 Evo gun level curve

Evo skins run **L1 → L7**. Stats improve at only some levels; the rest are cosmetic:

- **L1** — new look + the opening stat shape (e.g. Damage `+`, Fire Rate `+`, Reload `−−`)
- **L2** — kill announcement; penalty *shrinks* (`−−` → `−`)
- **L3** — look only
- **L4** — hit effect + larger damage step (`+` → `++`)
- **L5** — kill effect + firing effect
- **L6** — utility bonus (extra damage vs gloo walls)
- **L7** — exclusive emote + final look

So: **~3 stat steps across 7 levels; 4 of the 7 levels are pure cosmetic.** Garena charges for the
climb and pays most of it out in spectacle, not power. Deliberate.

### 2.5 Base guns vs skinned guns

A base (unskinned) gun is the table row, full stop. Nothing in Free Fire reduces a base gun. A skin
is strictly additive-with-a-tax. Skins do not stack — one skin per gun, equipped in the Collection
screen, and the loadout applies on spawn and to looted copies of that gun.

### 2.6 The fairness criticism — and what it's actually about

- Gurugamer calls the system "somewhat pay-to-win"; the sharpest contrast drawn is with PUBG Mobile,
  whose skins are **purely cosmetic and do not affect gameplay in any way**.
- The real grievance is not magnitude, it's **asymmetry of information and access**: boosts are
  behind Diamonds, Lucky Royale and Faded Wheel (randomised), so a F2P player cannot plan to earn
  the same gun.
- Counterweights Garena leans on: most skins are obtainable free via events/codes/Elite Pass; the
  boosts are small enough that guides call them "not game-breaking"; and the mandatory negative
  means some skins are *worse* for your playstyle (the SPAS12 Eye of Pharaoh and XM8 Aurous Dragon
  are both widely called skins you don't want to equip).
- **Failure mode to avoid:** a skin that is a net negative. If the tax can exceed the benefit, you
  have sold a trap, and refund/goodwill cost follows.

**What we take:** the +/− bar presentation, the 2-up-1-down shape, negatives confined to sustain
attributes, levels that pay mostly in spectacle.
**What we reject:** randomised acquisition, hidden magnitudes, and net-negative items.

---

## 3. Constraints (the Stone of Truth part)

| Constraint | Value | Consequence |
|---|---|---|
| Authority | **Listen-server host is authoritative** for damage; there is no dedicated server | the host must receive loadouts from the API, never from peers |
| Trust | A player's own phone can be **modded** | no stat ever travels client→host; only the API's word counts |
| Scale | 8 players (Classic) / 50 (Real Map BR) | one extra API read per room is free; per-shot API calls are not |
| Latency | Admit already round-trips to the API | loadout rides that same response — zero new round-trips |
| Consistency | Loadout is read **once, at admit** and frozen for the match | equipping mid-match must not change stats; prevents a mid-round "upgrade and respawn stronger" exploit |
| Bandwidth | Replicate `GunId` + `Level` (2 small fields) | all modifier maths is server-side; clients replicate only enough to tint the mesh and draw the HUD bar |
| What fails first | **Perceived fairness**, long before the CPU does | the cap is the load-bearing wall |
| No armor system | Campus Combat has no armor/helmet layer today | **Armor Penetration is defined but inactive in v1** |

### Assumptions (challenge these)

1. Max level is **4 for `m4a1`** (3 upgrade costs) and **5 for every other gun** — from
   `catalog_items.upgrade_costs` in `004_store.up.sql`. Max level = `1 + upgrade_costs.length`.
2. A player equips **one gun** (`PUT /v1/me/loadout {gun_id}`); there is no secondary slot yet.
3. The equipped gun is what the player **spawns with**; ground loot can still hand them other guns
   mid-match (see §6.3 for how that interacts).
4. No per-gun *skins* as separate SKUs — **owning the gun IS the skin**. One item, one upgrade track.
5. Movement Speed and Armor Penetration are reserved; v1 ships six live attributes.
6. BR (Real Map, 50p) is the competitive mode where fairness matters most.

---

## 4. BASE stats — what everyone gets

This is the ground-loot / unowned / fallback row. **These are the numbers already in
`CampusWeapons.cpp` — this table ratifies them, it does not change them.** The engine table is the
source of truth for feel; `catalog_items.stats` is the store's display copy of the same guns.

| id | Class | Dmg | RPM | Mag | Reserve | Reload s | Spread° | Recoil ↑/↔ | Falloff start→end (cm) | Min share | Range cm | Head × | Limb × | Max zoom | Loot tier |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `m4a1` | AR | 18 | 600 | 30 | 120 | 2.1 | 2.5 | 0.80 / 0.30 | 3000 → 6000 | 0.70 | 10000 | 2.00 | 0.75 | 4× | 1 |
| `ak47` | AR | 24 | 600 | 30 | 120 | 2.3 | 1.9 | 1.15 / 0.45 | 3250 → 6500 | 0.70 | 11000 | 2.00 | 0.75 | 4× | 3 |
| `m416` | AR | 19 | 700 | 30 | 120 | 2.0 | 1.7 | 0.66 / 0.22 | 3500 → 7000 | 0.70 | 11000 | 2.00 | 0.75 | 4× | 2 |
| `scar` | AR | 21 | 620 | 30 | 120 | 2.1 | 2.1 | 0.84 / 0.28 | 3250 → 6500 | 0.70 | 11000 | 2.00 | 0.75 | 4× | 2 |
| `an94` | AR | 23 | 680 | 30 | 120 | 2.4 | 2.2 | 1.00 / 0.36 | 3250 → 6500 | 0.70 | 11000 | 2.00 | 0.75 | 4× | 3 |
| `xm8` | AR | 17 | 720 | 30 | 120 | 1.8 | 1.8 | 0.70 / 0.24 | 3000 → 6000 | 0.70 | 10000 | 2.00 | 0.75 | 4× | 2 |
| `mp40` | SMG | 14 | 900 | 32 | 128 | 1.7 | 3.2 | 0.52 / 0.40 | 1500 → 3000 | 0.55 | 7000 | 1.80 | 0.75 | 2× | 1 |
| `m249` | LMG | 20 | 700 | 100 | 200 | 6.0 | 3.3 | 0.95 / 0.50 | 3500 → 7000 | 0.75 | 12000 | 1.75 | 0.75 | 2× | 3 |
| `deagle` | Pistol | 34 | 200 | 7 | 35 | 1.5 | 2.3 | 1.70 / 0.55 | 2500 → 5000 | 0.60 | 9000 | 2.20 | 0.75 | 2× | 1 |

Derived: `FireInterval = 60 / RPM`. ADS spread = `SpreadDeg × AdsSpreadShare` (0.25 AR, 0.35 SMG,
0.40 LMG, 0.20 pistol). Store `accuracy` ↔ engine spread: `SpreadDeg ≈ (100 − accuracy) / 12`.

**Reading the base table as a balance statement:** `ak47` is the damage king and a tier-3 loot drop;
`m416` is the accuracy/handling pick; `deagle` is the two-shot gamble; `mp40` owns rooms; `m249`
owns corridors and loses any fight it must reload in; `m4a1` is the honest baseline every player
starts with. A skilled player on the free `m4a1` must be able to beat an unskilled player on a
maxed `ak47` — that is the test every number below has to pass.

---

## 5. PURCHASED modifiers — the owned/upgraded gun

### 5.1 The rule

> **2 positives + 1 negative. Per-attribute hard clamp of ±25%. Shipped values stop well short of it.**

- Owning a gun grants its **trait**: one primary positive, one secondary positive, one negative.
- Level scales the **positives only**. The negative is **flat at every level** — the tax never
  shrinks, so the cost of the advantage stays visible for the life of the item.
- Negatives land only on **Reload, Magazine, or Range** — never on Damage or Accuracy. (Free Fire's
  own pattern; it keeps the penalty out of the duel and makes a net-negative item impossible.)
- Damage is capped hardest, because damage crosses shots-to-kill breakpoints and a breakpoint is a
  step function, not a 10% nudge.
- `m4a1` is **free and starter-owned** → it gets the smallest positives and **no negative at all**.
  The free gun must never be a trap.

### 5.2 Per-attribute caps (the engine's clamp)

| Attribute | Direction of "better" | Max total at max level | Hard clamp | Why this ceiling |
|---|---|---|---|---|
| Damage | ↑ | **+10%** | ±25% | shifts shots-to-kill; 18→19.8 dmg keeps 6-shot TTK on a 100 HP body |
| Rate of Fire (RPM) | ↑ | **+18%** | ±25% | the most *felt* buff; owner's headline perk |
| Accuracy (spread ↓) | ↑ (spread −) | **+20%** | ±25% | forgiving rather than lethal; cheap to grant |
| Reload Speed (seconds ↓) | ↑ (seconds −) | **+20%** | ±25% | mostly out-of-combat |
| Magazine | ↑ | **+15%**, rounded to whole rounds | ±25% | sustain, not burst |
| Range / falloff distance | ↑ | **+15%** | ±25% | moves falloff start+end together |
| Recoil control (recoil ↓) | ↑ (degrees −) | **+20%** | ±25% | pairs with accuracy as a "handling" buff |
| Movement Speed (while holding) | ↑ | **+5%** (reserved, OFF in v1) | ±10% | highest felt advantage per point; most dangerous |
| Armor Penetration | ↑ | n/a (reserved, OFF in v1) | — | no armor layer exists yet |

### 5.3 Level ladder (share of the cap)

| Level | Share of cap | Also unlocks (non-stat) |
|---|---|---|
| 1 (owned) | **50%** | shop tint + name plate |
| 2 | **62.5%** | kill-feed flourish |
| 3 | **75%** | muzzle flash variant |
| 4 | **87.5%** | tracer colour |
| 5 (max) | **100%** | kill effect + lobby showcase |

Secondary positive uses **half the primary attribute's cap**, on the same ladder.
`m4a1` max level is 4, so the starter tops out at 87.5% of its (already small) caps.

Formula: `bonus(L) = cap × (0.5 + 0.125 × (L − 1))`, L ∈ [1, 5].

### 5.4 Per-gun trait table (shipped values)

Positives shown as **L1 → max**. Negative is flat.

| id | Primary + | Secondary + | Negative − | L1 shape (the +/− bar) | Max shape |
|---|---|---|---|---|---|
| `m4a1` ★free | Accuracy **+10% → +17.5%** | Reload **+5% → +8.75%** | *none* | Accuracy `+`, Reload `+` | Accuracy `++`, Reload `+` |
| `ak47` | Damage **+5% → +10%** | Fire Rate **+4.5% → +9%** | Magazine **−10%** | Dmg `+`, RoF `+`, Mag `−` | Dmg `++`, RoF `+`, Mag `−` |
| `m416` | Accuracy **+10% → +20%** | Recoil ctrl **+5% → +10%** | Reload **−10%** | Acc `+`, Recoil `+`, Reload `−` | Acc `++`, Recoil `+`, Reload `−` |
| `scar` | Damage **+5% → +10%** | Accuracy **+5% → +10%** | Reload **−10%** | Dmg `+`, Acc `+`, Reload `−` | Dmg `++`, Acc `+`, Reload `−` |
| `an94` | Fire Rate **+9% → +18%** | Damage **+2.5% → +5%** | Magazine **−10%** | RoF `+`, Dmg `+`, Mag `−` | RoF `++`, Dmg `+`, Mag `−` |
| `xm8` | Fire Rate **+9% → +18%** | Accuracy **+5% → +10%** | Range **−10%** | RoF `+`, Acc `+`, Range `−` | RoF `++`, Acc `+`, Range `−` |
| `mp40` | Fire Rate **+9% → +18%** | Magazine **+7.5% → +15%** | Range **−10%** | RoF `+`, Mag `+`, Range `−` | RoF `++`, Mag `+`, Range `−` |
| `m249` | Reload **+10% → +20%** | Accuracy **+5% → +10%** | Magazine **−10%** | Reload `+`, Acc `+`, Mag `−` | Reload `++`, Acc `+`, Mag `−` |
| `deagle` | Fire Rate **+9% → +18%** | Reload **+10% → +20%** | Magazine **−10%** (7→6) | RoF `+`, Reload `+`, Mag `−` | RoF `++`, Reload `+`, Mag `−` |

`xm8` and `m416` are the two guns that deliver the owner's stated brief —
**"faster fire rate, better accuracy"** — in its purest form.

### 5.5 Worked examples (base → max-level owned)

| Gun | Stat | Base | Max owned | Δ | Effect |
|---|---|---|---|---|---|
| `xm8` | RPM | 720 | **850** | +18% | 0.083 s → 0.071 s between shots |
| `xm8` | Spread° | 1.8 | **1.62** | −10% | tighter hip-fire cone |
| `xm8` | Range cm | 10000 | **9000** | −10% | the tax: loses the long quad shot |
| `ak47` | Damage | 24 | **26.4** | +10% | 5-shot body kill → still 4 shots at 100 HP w/ a head |
| `ak47` | Mag | 30 | **27** | −10% | 3 fewer rounds per load |
| `m416` | Spread° | 1.7 | **1.36** | −20% | the tightest cone in the game |
| `m416` | Reload s | 2.0 | **2.2** | −10% | the tax |
| `m4a1` | Spread° | 2.5 | **2.19** | −17.5% (L4) | free gun gets real, no-cost help |
| `deagle` | Mag | 7 | **6** | −10% | one fewer chance — a real cost on a 7-round gun |

### 5.6 Does the cap hold? The fairness test

Max-owned `ak47` at 26.4 dmg / 600 RPM vs base `ak47` at 24 dmg / 600 RPM: **the owner wins a
perfectly even trade by ~0.2 s over a full magazine.** That is an edge. It is not an instant win.
Reaction time variance between two human players is larger than every number in §5.4. Aim beats
ownership — which is the only balance invariant worth defending.

**Expected total advantage, max-level owner vs base-gun player, same skill: 6–9% TTK.**
Industry reference: a scope attachment in PUBG is worth more than this.

### 5.7 Options considered

- **A) Cosmetic only (PUBG model).** Optimises for competitive integrity and zero fairness
  criticism. Costs: guns stop being a meaningful purchase and `catalog_items.stats`, `per_level`,
  `upgrade_costs` all become decoration — a real revenue cut on an already-built store.
- **B) Capped 2-up-1-down, server-authoritative (this document).** Optimises for a store that sells
  something real while keeping skill dominant. Costs: a permanent balance obligation, and the
  "pay-to-win" accusation never fully goes away.
- **C) Free Fire-grade boosts (±20–25% on damage, randomised acquisition).** Optimises for revenue
  per whale. Costs: ranked becomes a wallet check; the randomised path drags in Play's loot-box odds
  disclosure requirements and India's gambling-adjacent scrutiny.

**Recommendation: B, with C's acquisition model explicitly rejected** — deterministic named
purchases keep us out of loot-box territory entirely, and §7's `skin_stats` switch gives us A for
free wherever integrity matters more than the sale.

---

## 6. How ownership reaches the match

### 6.1 The threat

The host is a student's phone. If the host asks the joining client "what gun do you have?", a modded
APK answers "maxed AK-47" and the whole cap in §5 is theatre. **The client is never asked.**

### 6.2 The flow (extends what already exists)

```
phone                          API                                     host phone
  │                             │                                          │
  │─ PUT /v1/me/loadout ───────▶│  {gun_id}  → players.equipped_gun_id     │
  │                             │  (validated: must be in player_items     │
  │                             │   or catalog_items.starter)              │
  │                             │                                          │
  │─ POST /v1/rooms/join ──────▶│                                          │
  │◀── {room:{connect}, ticket} │   ticket = signed, short-lived,          │
  │                             │   room-scoped (unchanged)                │
  │                             │                                          │
  │═══ connect + present ticket ════════════════════════════════════════▶  │
  │                             │                                          │
  │                             │◀── POST /v1/rooms/:code/admit {ticket} ──│
  │                             │                                          │
  │                             │─── {player_id, name, loadout:{...}} ────▶│  ← THE ONLY
  │                             │                                          │    SOURCE
  │                             │                                          │    OF TRUTH
```

**One new field on a response that already exists. No new round-trip, no new endpoint required.**

### 6.3 Contract change — `POST /v1/rooms/:code/admit`

Host-only, auth required, rate-limited 120/min (all unchanged).

Request (unchanged): `{ "ticket": "<signed room-scoped ticket>" }`

Response **today**:
```json
{ "player_id": "uuid", "name": "Arjun" }
```

Response **after this change** (additive, backward compatible):
```json
{
  "player_id": "uuid",
  "name": "Arjun",
  "loadout": {
    "gun_id": "xm8",
    "level": 3,
    "max_level": 5,
    "source": "purchased",
    "stats_enabled": true
  }
}
```

| Field | Type | Rules |
|---|---|---|
| `gun_id` | string, `^[a-z0-9_]{2,32}$` | resolved server-side from `players.equipped_gun_id`; **must** be a `kind='gun'` row in `catalog_items`, and owned (`player_items`) or `starter` — otherwise falls back to `m4a1` |
| `level` | int 1..`max_level` | `player_items.level`; `1` for the starter |
| `max_level` | int | `1 + array_length(upgrade_costs)` — lets the host validate without its own copy |
| `source` | `"purchased"` \| `"starter"` \| `"fallback"` | `"fallback"` = equipped gun was unowned/unknown; host logs it, player gets base `m4a1` |
| `stats_enabled` | bool | **the room's** `skin_stats` flag (§8), echoed here so the host has one field to read |

Errors unchanged: `403 TICKET_INVALID`, `404` unknown room. A loadout that cannot be resolved **never
fails admit** — the player gets `m4a1` base and the host logs it. Matching the existing precedent in
`/result`, where an unknown `gun_id` is dropped rather than refused (§6.8 of the rooms doc).

### 6.4 Bulk read — `GET /v1/rooms/:code/loadouts`

Host-only. For reconnects, late host migration, or a host that lost its in-memory map.

```json
{
  "stats_enabled": true,
  "players": [
    { "player_id": "uuid", "gun_id": "xm8",  "level": 3, "source": "purchased" },
    { "player_id": "uuid", "gun_id": "m4a1", "level": 1, "source": "starter"   }
  ]
}
```

Members-only visibility rules identical to `GET /v1/rooms/:code`. Cacheable 10 s. Only rows with
`admitted_at IS NOT NULL` are returned.

### 6.5 Freeze semantics

The host **snapshots** the loadout at admit and uses that snapshot for the whole match. A player who
buys an upgrade mid-match does not get it until the next match. This closes the
"die → upgrade → respawn stronger" exploit and makes a match replayable from one record.

### 6.6 Inside the match

1. Host stores `{GunId, Level, bStatsEnabled}` on the player's `PlayerState` from the admit response.
2. Host computes the **effective** `FCampusWeaponDef` once per player per gun: base row from
   `CampusWeapons::FindOrStarter(GunId)`, then the §5.4 trait at the §5.3 ladder share, then clamp
   every multiplier to `[0.75, 1.25]`, then recompute derived fields (`FireInterval`, ADS spread).
3. **Only the host's copy is ever used for hit registration, damage, and fire-interval validation.**
4. Replicated to clients: `GunId` (FName) + `Level` (uint8) only — enough for mesh tint, HUD label,
   and the +/− bar. Never the computed stats.
5. **Looted guns are base.** A player who owns a maxed `xm8` and picks up a ground `ak47` fires a
   **base** `ak47`. The modifier belongs to the *owned* gun, not to the player. This is the single
   most important fairness property in the system: it means ground loot is always an honest,
   identical object for all 50 players in a BR lobby.
6. Mid-match swap back to the owned gun restores the modifier (the snapshot is keyed by gun id).

### 6.7 Schema deltas

| Change | Forward | Rollback |
|---|---|---|
| `players.equipped_gun_id text REFERENCES catalog_items(id)` — if `PUT /v1/me/loadout` does not already persist it | `ALTER TABLE players ADD COLUMN equipped_gun_id text REFERENCES catalog_items(id)` (nullable → NULL means `m4a1`) | `DROP COLUMN` — safe, nullable, no backfill |
| `rooms.skin_stats boolean NOT NULL DEFAULT true` | add with default; existing rooms read `true` = today's behaviour | `DROP COLUMN`; host falls back to `true` |
| no change to `player_items` | `level` already exists and already carries upgrades | — |

**Two-phase for `rooms.skin_stats`:** (1) add the column + serve it in `roomView` and `admit`, host
treats a missing field as `true`; (2) once the client build that reads it is >90% adopted, let the
create-room UI write `false`. Nothing is dropped or renamed, so phase 2 is reversible by a flag.

### 6.8 Store display

`itemView()` in `store.js` already projects per-level stats (`damage`, `accuracy`). Extend it to emit
the full §5.4 shape so the shop shows the same +/− bars the match honours:

```json
"attributes": {
  "base":  { "damage": 17, "fire_rate": 1.4, "accuracy": 78, "range": 60, "mag": 30 },
  "bonus": [
    { "attr": "fire_rate", "pct": 13.5, "bars": 2 },
    { "attr": "accuracy",  "pct": 7.5,  "bars": 1 },
    { "attr": "range",     "pct": -10,  "bars": -1 }
  ],
  "at_max": [
    { "attr": "fire_rate", "pct": 18, "bars": 2 },
    { "attr": "accuracy",  "pct": 10, "bars": 1 },
    { "attr": "range",     "pct": -10, "bars": -1 }
  ]
}
```

`bars` maps magnitude to the Free Fire-style display: `1` for ≤10%, `2` for >10%, negative for the
tax. **Unlike Free Fire, we also print the percentage.** Hiding the number is what turns a small
advantage into a conspiracy theory.

**Where the trait table lives:** in `catalog_items.per_level` (JSONB, already exists), *not* hardcoded
twice. Shape: `{"primary":{"attr":"fire_rate","cap_pct":18},"secondary":{"attr":"accuracy","cap_pct":10},"negative":{"attr":"range","pct":-10}}`.
The engine keeps a compiled copy for offline/LAN play, and the backend value wins when they disagree
(the API is authoritative; the engine copy is the fallback). A balance change is then **a SQL update,
not a client release** — which is the whole reason to put it there.

---

## 7. Fairness switch: `skin_stats`

| Mode | `skin_stats` | Behaviour | Default for |
|---|---|---|---|
| **Loadout** | `true` | §5 modifiers apply to owned guns | Casual Classic rooms, friend-code customs |
| **Flat** | `false` | §4 base table only; ownership affects **appearance only** (tint, tracer, kill effect) | **Ranked, BR (Real Map 50p), tournaments, esports customs** |

- Set at room creation (`POST /v1/rooms` body: `skin_stats: boolean, default true`), immutable for
  the room's life, returned in `roomView` and echoed in `admit.loadout.stats_enabled`.
- Shown as a **badge in the room list and on the pre-match screen** — "FLAT STATS" / "LOADOUT STATS".
  A player must never discover the mode by losing a fight.
- In Flat mode the purchased gun still *looks* bought: tint, tracer colour, kill-feed flourish, lobby
  showcase. **The spectacle is what we actually sell; the stats are the garnish.**
- **Recommendation: ship BR and Ranked as Flat from day one.** It costs nothing (the cosmetic layer
  carries the purchase value) and it permanently removes the one criticism that killed goodwill for
  Free Fire. It is far easier to turn stats *on* later than to take them away from paying players.

---

## 8. Legal note (India) — not legal advice

**Verify with counsel before launch. This is a design-level read, written 2026-10, and the
regulatory position is actively moving.**

1. **Promotion and Regulation of Online Gaming Act, 2025 (India).** The prohibition targets *online
   money gaming* — games played by depositing money or stakes **in expectation of monetary winnings
   or other enrichment**. Campus Combat sells guns for **Diamonds** and Diamonds are bought with real
   money, but **nothing in the system pays out money or money-equivalent value**. There is no wager,
   no prize pool, no cash-out. A stat-bearing item is therefore a **durable in-game good**, in the
   same bucket as a cosmetic — the stat bonus does not make it a stake.
2. **The line you must never cross:** no conversion of Diamonds, items, or match outcomes into cash,
   bank credit, gift cards, crypto, or withdrawable balance; and no entry fee paid against a prize.
   `POST /v1/store/items/:id/sell` already returns **Diamonds only** at `sell_ratio` — never INR.
   **Keep it that way; that endpoint is the single most legally sensitive line of code in the store.**
   Any future "cash out" or paid-entry tournament changes the legal class of the whole product.
3. **No randomised acquisition — keep it that way.** Guns are sold as deterministic, named items at
   published prices. Introducing a Lucky Royale / Faded Wheel style gacha for stat-bearing guns would
   pull in loot-box odds-disclosure obligations (Play policy), consumer-protection scrutiny on
   chance-based purchases, and the gambling-adjacency argument the 2025 Act's reasoning leans on.
   **This is the single highest-value legal decision already made in the design. Do not reverse it.**
4. **Google Play policy** — stat-affecting paid items are standard across the category (Free Fire
   itself ships on Play with exactly this system). Requirements we must meet: prices and what the
   purchase grants disclosed **before** purchase (§6.8 covers it), purchases verified server-side
   (`store.js` already exchanges the purchase token with Google), and **no misrepresentation of what
   a purchase does** — so the shop's stated percentages must equal the match's applied percentages.
   The shared `per_level` source in §6.8 is what makes that true by construction rather than by
   discipline.
5. **Advertising/consumer-law hygiene:** do not market a gun as "unbeatable" or "guaranteed wins".
   §5.6's honest framing — *an edge, not an instant win* — is also the compliant framing.
6. **GST / fees** are Google's as seller of record on Play purchases; the Razorpay path (off unless
   `RAZORPAY_ENABLED=true`) is ours and carries its own GST obligations. Out of scope here.
7. **Age gate.** Paid stat items plus a minor audience raise parental-consent and, with `009_adult_college`
   already in the migration set, DPDP-consent questions. **Route this to a privacy/compliance review
   before launch.**

---

## 9. Rollout plan

1. **Ratify §4.** Weapons agent lands `CampusWeapons.{h,cpp}` with the base table exactly as above.
   Base-only build. Shippable on its own.
2. **Migration `0NN_gun_attributes`.** Add `players.equipped_gun_id` (if absent) + `rooms.skin_stats`
   (default `true`). Write the §5.4 traits into `catalog_items.per_level`. Both reversible by
   `DROP COLUMN`; the `per_level` rewrite ships with its own `.down.sql` restoring the old
   `{"damage_pct":n}` values.
3. **API.** Extend `admit` with `loadout`; add `GET /v1/rooms/:code/loadouts`; extend `itemView` with
   `attributes`. All additive — old clients ignore the new fields.
4. **Engine.** Host reads `loadout` at admit, snapshots it, applies trait + clamp. Clients get
   `GunId` + `Level` only. Behind a cvar/config switch so it can be killed without a rebuild.
5. **Balance pass.** `kamish` to attack it: max-owned vs base TTK across all 9 guns, at 5 m / 20 m /
   60 m, hip and ADS. **Gate: no matchup where ownership alone flips the winner between two players
   of equal aim.**
6. **BR/Ranked default to Flat.** Ship the badge in the room list in the same build.
7. **Watch.** Win-rate by `gun_id` × `level` from `match_results` (`gun_id` is already recorded per
   player in `/result`). A level-5 win rate more than **3 points** above level-1 on the same gun means
   the cap is too loose — and because the traits live in `per_level`, the fix is one SQL statement.

---

## 10. Open questions (need the owner's call)

1. **Is BR/Ranked Flat from day one?** My recommendation: yes. Needs a product decision because it
   changes the sales pitch for guns from "stronger" to "cooler".
2. **Owner asked for ~+25%.** I recommend **+18% fire rate / +20% accuracy** instead, with a ±25%
   hard clamp as the engine's guard rail. If +25% is required, it is one constant per row in
   `per_level` — but §5.6's fairness test then fails at the margin, and the ranked/BR Flat default
   becomes mandatory rather than advisable.
3. **Should `m4a1` (free, starter) get any bonus at all?** I gave it small positives and no negative
   so that F2P players feel progression. The alternative — starter is always pure base — is a cleaner
   fairness story but a flatter F2P experience.
4. **Movement Speed and Armor Penetration:** confirmed reserved/OFF for v1? Armor Penetration needs an
   armor system before it can mean anything.
5. **Do looted guns ever inherit the owner's modifier?** I say **never** (§6.6.5). Confirm — it is the
   property the entire fairness argument rests on.
6. **Separate skin SKUs later?** Today owning the gun *is* the skin. If multiple skins per gun are ever
   sold, §5's "one trait per gun" becomes "one trait per equipped skin" and `player_items` needs a
   `skin_id`. Worth knowing now, because it is a schema shape, not a feature flag.

---

> The Stone of Truth shows the constraint before the code: what fails first here is not the frame
> rate, it is the player's belief that the fight was fair. The cap in §5.2 and the Flat switch in §7
> are the load-bearing line. A house raised on that foundation does not fall.
