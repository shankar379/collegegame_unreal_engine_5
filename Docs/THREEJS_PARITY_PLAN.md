# Three.js → Unreal parity plan (2026-10-22)

What the three.js build learned on 2026-10-21/22, and where each item lands in this project.
Status: [x] done, [~] in progress, [ ] open.

## Already in Unreal (checked, no work needed)
- Solo / duo / squad → READY → START → backend battle session (50 slots, teams kept together,
  10 s countdown, IN_FLIGHT, IN_PROGRESS) — `campuscombat-backend/api/src/lib/battlesession`.
- Waiting island grid 8 × 7 cells with min separation (no overlap) — `CampusBRWait` in `CampusBRPlane.cpp`.
- Zone damage and fall damage hit bots too (`CampusBRZone.cpp` iterates every `ACampusCharacter`,
  fall damage is `ACampusCharacter::Landed`).
- Bots walk to the safe zone (`FindZone` in `CampusBot.cpp`).
- Diamonds bought with Google Play Billing (migration 030); guns and cosmetics bought with Diamonds.

## Gaps, in build order
1. [x] **Weapon Library V3 guns.** Imported 2026-10-22 (9 guns, 1024 px, 4 skin textures); `CampusWeapons::MeshPathFor` → V3 with low-poly and rifle fallbacks, muzzle from the `Muzzle` socket, `/Game/Weapons/LibraryV3` cooked. Import the 9 catalogue guns (m4a1 ak47 m416 scar an94 xm8 mp40 m249
   deagle) with `Scripts/weapon_v3/import_weapon_library_v3.py` (`WEAPON_V3_FILTER`), textures capped at
   1024 for the APK (size rule: never ship heavier than needed). Point `CampusGunMeshes::MeshPath` at
   `/Game/Weapons/LibraryV3/Meshes/SM_<id>`, keep the old low-poly mesh as fallback, read the muzzle from
   the mesh's `Muzzle` socket. Single mesh per gun (no magazine animation), same as three.js.
2. [x] **Skins with abilities, four per gun** (owner, 2026-10-22): Royal, Neon Hex, Arctic Circuit, Gold Relic - each two ups and one down (`CampusGunSkins`, migration 032 `skin_mods`), worn per gun type, applied to picked-up guns too, capped by `FCampusWeaponMods::Clamped`. `PUT /v1/me/gunskins`, admin "Fits gun" picker, replicated `GunSkinStyles` painted on `M_WeaponV3`. Today the `gunskin` slot allows ONE skin for all guns.
   Migration 032: `catalog_items.for_gun`, table `player_gun_skins (player_id, gun_id, item_id)`;
   seed `skin_<gun>_royal` rows (Diamonds). API: list/equip per gun. Admin: skin rows in Economy.
   Game: `M_WeaponV3` instance parameters (`SkinTexture = T_Skin_RoyalGold`, `PatternMode = 1`,
   `SkinStrength = 1`) applied on the gun component; skin replicated with the weapon id.
3. [x] **Weapons page** (`CampusWeaponsUI.cpp`, lobby WEAPON button): gun list, a real 3D gun on the lobby scene turned by drag (`ShowWeaponStage`), attributes with the skin's arrows, skins row, BUY / EQUIP. Still to add: Show in Lobby. (Free Fire style): owned guns list, 360° turntable, skins row, EQUIP.
4. [x] **Spectate teammates only — bug:** when the squadmate being watched dies, the pawn is destroyed
   and the camera is left on nothing. Re-target every dead mate watching that pawn to the next living
   mate; NEXT TEAMMATE button on the BR HUD (`ServerSpectateNext`, own squad only).
5. [x] **Squad jumps together:** (`ACampusBRDrop::DropWithSquad`, mates 2.5 m apart) when one squad member drops from the plane, every squadmate still aboard
   drops with them (three.js `dr` 3→other rule).
6. [x] **Bots must loot:** (`StripWeapons` for bots promoted in the first 60 s, `TickLoot`, 35 s failsafe, longer burst pauses) in battle royale bots start unarmed (no starter rifle) and walk to the nearest
   ground gun inside the zone; slower firing (longer burst pauses).
7. [ ] **Phone test** (`BuildAndroid.bat`), then fix what it finds — ROADMAP line 11-12.

## Rules carried over
- Never ship a bigger asset than the one it replaces unless it is the approved art.
- Deploy / push only when the owner asks. Build with `BuildAndroid.bat`; never delete
  `DerivedDataCache`, `Saved\Cooked`, `Intermediate`, `Binaries`.

## Importing the V3 guns (once, with the editor closed)
```
cd C:\UE\Projects\CampusCombat\CollegeCampusGame
set WEAPON_V3_FILTER=m4a1,ak47,m416,scar,an94,xm8,mp40,m249,deagle
set WEAPON_V3_MAX_TEX=1024
E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe "%CD%\CollegeCampusGame.uproject" -run=pythonscript -script="Scripts/weapon_v3/import_weapon_library_v3.py"
```
Ends with `WEAPON_V3_IMPORT_COMPLETE 9`. Until it has run, every gun falls back to the old low-poly mesh.
