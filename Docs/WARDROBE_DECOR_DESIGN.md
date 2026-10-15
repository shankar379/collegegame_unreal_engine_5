# Wardrobe and Lobby Decoration

As of 2026-10-15. Game commit `3ff548f` (+ `c147cb2`), backend commit `99d74d8`.

## Decisions (owner)

| Decision | Choice |
| --- | --- |
| Clothing | Full outfit meshes: one whole-body skeletal mesh per outfit plus material overrides |
| Decoration placement | 8 fixed anchors around the lobby character |
| Wardrobe contents | Owned items only; buying happens in the store |
| Currency | Diamonds only; cosmetics cannot be sold |
| Presets | 3 saved outfits |

## How it works

- **Catalog:** `Source/CollegeCampusGame/Campus/CampusCosmetics.h` holds outfits and decor. Unknown ids fall back to `outfit_starter`.
- **Applying an outfit:** `CampusOutfit::Apply(USkeletalMeshComponent*, FName)` sets the mesh and colours on any figure (lobby, wardrobe, in-match character). The animation class survives a mesh swap.
- **Colours (V1):** the mannequin's default material has no parameters, so outfits use `M_UEFN_Mannequin_Masked_Bicolor` with `Primary Color` / `Secondary Color` (+ roughness, metallic). `outfit_starter` keeps the authored texture.
- **In matches:** `ACampusCharacter::OutfitId` replicates (same pattern as the gun loadout); the server validates the id against the catalog.
- **Saving:** `UCampusSave` is the single source on the phone; changes save locally first, then sync to the backend.
- **Screens:** `CampusWardrobeUI.cpp` (WARDROBE tab under CHARACTER in the lobby; LOBBY DECOR opens from the wardrobe as a bottom sheet).

## Items (V1)

| Id | Kind | Price (Diamonds) |
| --- | --- | --- |
| outfit_starter | outfit | free, everyone owns it |
| outfit_hoodie | outfit | 300 |
| outfit_varsity | outfit | 400 |
| outfit_tactical | outfit | 600 (uses `SKM_UEFN_Mannequin`, tests the mesh-swap path) |
| decor_cone | decor | 100 |
| decor_banner | decor | 150 (overhead anchor only) |
| decor_bench | decor | 200 |
| decor_speaker | decor | 300 |
| decor_trophy | decor | 400 |
| decor_bike | decor | 500 |

V1 art is placeholder: outfits are two-colour tints of the mannequin, decor is built from engine basic shapes.

## Decor anchors (lobby cm, Z from floor)

| Anchor | X | Y | Z |
| --- | --- | --- | --- |
| floor_l / floor_r | -40 | ±215 | 0 |
| floor_back_l / floor_back_r | -210 | ±165 | 0 |
| wall_l / wall_r | -230 | ±320 | 0 |
| pedestal | -165 | 0 | 0 |
| overhead | -260 | 0 | 300 (hangs) |

## Backend API

Migration `021_cosmetics`. All routes need login.

| Method and path | Purpose |
| --- | --- |
| `GET /v1/me/cosmetics` | Equipped, presets "1"-"3", decor, owned items |
| `PUT /v1/me/cosmetics` | Replace equipped set (Idempotency-Key required) |
| `PUT /v1/me/cosmetics/presets/:idx` | Save a preset |
| `POST /v1/me/cosmetics/presets/:idx/apply` | Wear a preset (ownership re-checked) |
| `PUT /v1/me/lobby` | Replace the whole decor map (send all 8 anchors, null = empty) |
| `GET /v1/players/:id/cosmetics` | Another player's equipped items |
| `POST /v1/store/items/:id/buy` | Buy an outfit or decor item |

Errors: `409 NOT_OWNED`, `422 SLOT_MISMATCH`, `422 UNKNOWN_ANCHOR`, `422 NOT_DECOR`.

## Not yet verified on a device

- Which body regions get the primary vs secondary colour.
- Engine shape pivots (cone and trophy may sit low).
- `overhead` framing on 20:9 screens.
- Two phones seeing each other's outfit in a match.

## Next: real art

1. Outfits: download 3 to 5 free characters (Mixamo or Fab), re-skin them onto the UEFN skeleton with a Blender script, import as `SKM_Outfit_*`, point the catalog entries at them. No code change.
2. Decor: free Fab props (CC-BY, credited in `CREDITS.md`).
3. Rendered item icons for the wardrobe grid.
