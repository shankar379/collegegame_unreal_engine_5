# Campus Combat — Master Plan

Last updated: 2026-10-11. Keep this file in the repo; tick items as they ship.

---

## 1. Game modes

### 1.1 Two ways to play
| Mode | How you join | Cost | Size |
|---|---|---|---|
| **Quick Match** (random) | Press Play → server matches you with random players | Free | up to 50 |
| **Custom Room** (friends) | Create a room with a **Room Card**, invite friends by room code | 1 Room Card | your friends only |

### 1.2 Maps (chosen in the lobby)
| Map | Format | Players |
|---|---|---|
| **Classic** (short) | The college map. One room = **4 rounds**; first team to **3 round wins** wins (tie at the end → deciding round). Elimination rounds, safe zone closes. | small: Solo / Duo / Squad |
| **Real Map** (large) | Battle royale: one long match, last player / team standing wins | **up to 50** |

### 1.3 Team size (chosen in the lobby, works with both maps)
| Mode | Team | Real Map players | Teams |
|---|---|---|---|
| **Solo** | 1 | 50 | 50 individuals |
| **Duo** | 2 | 50 | 25 teams |
| **Squad** | 4 | **48** (50 does not divide by 4) | 12 teams |

Only one player / team wins a match.

---

## 2. Economy

### 2.1 Currencies
- **Room Cards** — create a Custom Room. **1 free card per player per day.**
- **Diamonds** — buy guns, skins and other items.

### 2.2 Example packs (prices to be confirmed)
| Pack | Price | You get |
|---|---|---|
| Room Cards x10 | ₹50 | 10 cards |
| Diamonds | ₹50 / ₹100 / ₹500 | 250 / 550 / 3000 💎 |
| **Combo offer** | ₹100 | 10 cards + 500 💎 |

### 2.3 Rules
- Paid guns: **decision pending** — recommended: equal power, premium looks (skins, effects), or also unlockable by playing. Stronger paid guns = pay-to-win, drives free players away.
- Add GST to sales; publish a refund policy.
- Revenue covers the game servers.

### 2.4 Razorpay
| Part | What |
|---|---|
| App | Razorpay Android Checkout SDK (`com.razorpay:checkout`) via `CampusCombat_UPL.xml` + Java bridge + JNI |
| Backend | `razorpay` npm: product list, create order, **signed webhook** → wallet credit, purchase history |

Flow: shop tap → backend creates order (price set on the server) → Razorpay checkout (UPI / card) → Razorpay webhook (signature verified) → wallet credited → app refreshes.
- Keys only in backend `.env` (never in the app or git). App gets the Key ID only.
- Start in **Test Mode**.
- **Google Play:** digital items need Play Billing; India allows "user choice billing" (Razorpay alongside). Direct APK sharing: Razorpay alone is fine.

---

## 3. Engagement (keep students playing)

### 3.1 Leaderboards
| Board | Shows |
|---|---|
| Top players | name, roll no. (masked: `21XX…45`), branch, college, kills, K/D, wins |
| Branch war | CSE vs ECE vs MECH vs CIVIL … |
| College vs College | total score per college |
| This week | resets every Monday |
| Today's MVP | best player of the last 24 h |

### 3.2 Identity
- Verified student profile (college email or roll number).
- Profile photo upload (own photo or any image).
- Titles / badges ("CSE Sniper", "Top 10 – Week 3").
- Rank tiers: Bronze → Silver → Gold → Diamond → **Campus Legend**.

### 3.3 Daily hooks
- Daily missions ("Win 1 match", "5 headshots").
- 7-day login streak → cosmetic.
- Seasons (4–6 weeks) with a permanent **Hall of Fame**.

### 3.4 Events
- Inter-college tournament on fest days (big screen / Instagram Live).
- Branch weekend war (winning branch gets a lobby banner).
- Rivalry hour (kills vs the other college count double).

### 3.5 Social
- Post-match result card → WhatsApp / Instagram stories.
- Invite a friend → both rewarded.
- Squads with a squad name.
- College-themed skins (department jerseys, fest t-shirts).

### 3.6 Weekly Instagram auto-post
- Every weekend the backend picks: **Winner of the Week**, **Sniper of the Week** (headshots), **Kill Machine of the Week**.
- Generates a poster: player's uploaded photo + name, branch, college, title → posts to Instagram.
- Needs: Instagram Business account linked to a Facebook Page, Meta content-publishing API.
- Uploaded photos must be moderated before public posting — **decision pending**: fully automatic or one-tap approval.

### 3.7 Rollout
1. One college first, 20–30 "founder" players with an exclusive badge.
2. Posters with a QR code: *"Who's the best shooter in CSE?"*
3. Weekly leaderboard post on the college Instagram.
4. Add the second college → College vs College.

---

## 4. Combat: hit zones and damage numbers

### 4.1 Zones (from the bone the server trace hits)
| Zone | Bones | Multiplier |
|---|---|---|
| Head | head, neck | x2.0 |
| Body | pelvis, spine | x1.0 |
| Arms / hands | upperarm, lowerarm, hand | x0.8 (pending) |
| Legs / feet | thigh, calf, foot | x0.75 |

### 4.2 Weapons (from three.js `weapons.js`)
AK47 24 · M416 19 · SCAR 21 · M4A1 18 · AN94 23 · XM8 17 · MP40 14 · M249 20 · Desert Eagle 34 · Fist 15. One hit: min 1, max 250.
Range falloff: **pending** (proposal: full to 50 m, 70% at 150 m).

### 4.3 Damage numbers (three.js `hud.css .dmgnum`)
- White number at the hit point, rises ~46 px, fades in 0.9 s.
- Headshot: bigger red number + "HEADSHOT", red hit-marker.
- Server is authoritative: the shooter gets the real damage immediately (no claim / ack).
- Kill feed shows a headshot icon.

### 4.4 Gear
Helmet / vest reduce head / body damage — **pending** (needs the gear system).

---

## 5. Backend and servers
- Profile (name, roll no., branch, college, photo), wallet, daily free card, match stats.
- Rooms: spend a card → room code → invite.
- Matchmaking queues: Solo / Duo / Squad × Classic / Real Map.
- Leaderboards (weekly / all-time, branch, college).
- Weekly poster job + Instagram publish.
- Dedicated game servers on AWS: ~1 server per 1–2 matches; start ≈ ₹3–8k / month.
- Test server today: PC `192.168.1.5` (API 8080, game 7777), no Firebase.

---

## 6. Build order
1. [ ] **Lobby mode select**: map (Classic / Real Map) + team (Solo / Duo / Squad), remembered per player
2. [ ] Backend: profile + photo, wallet, daily free card, match stats
3. [ ] Razorpay: packs, orders, verified webhook, shop screen in the lobby
4. [ ] Rooms: card-gated create, invite by code
5. [ ] Matchmaking queues and team assignment (Solo 50 / Duo 25x2 / Squad 12x4)
6. [ ] Hit zones, weapon table, damage numbers
7. [ ] Leaderboards (lobby screen)
8. [ ] Weekly Instagram poster
9. [ ] Missions, streaks, seasons

## 7. Done so far (Unreal)
- Lobby + match levels, touch controls and HUD ported from three.js, LAN server.
- GASP animations: starts, stops, pivots, lean, turn in place, idle breaks, slide, hurdle, mantle / climb, prone (placeholder).
- College model imported from three.js with collision and 198 doors (L_Match).
- Kill floor below the map; lower jump.

## 8. Open decisions
1. Paid guns: cosmetic only, or stronger?
2. Instagram posts: automatic, or one-tap approval?
3. Arm damage multiplier x0.8?
4. Range falloff?
5. Helmet / vest damage reduction?
6. Final pack prices.
