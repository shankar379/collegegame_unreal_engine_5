# Reference refinement and reload exports

This pass changes MP40's angular butt into a closed tilted oval loop, changes its orange wood lower/grip to dark bakelite, and adds a removable magazine body and grip opening to Desert Eagle. Desert Eagle's grip assembly now leans back toward the heel to follow the supplied silver/black reference more closely. AK-47 geometry otherwise retains the existing V3 model. The remaining library models are unchanged by this pass. These are reference-inspired reconstructions, not exact reproductions of the original game's meshes.

## Editable sources

- `wv3_guns_classic.py`: MP40, Desert Eagle, M1887 geometry.
- `wv3_guns_ak.py`: AK-47 geometry.
- `wv3_kit.py`: bevels, extrusions, curved tubes and closed loops.
- `wv3_surface.py`: normal materials and common skin shader.
- `wv3_reload.py`: explicit membership of each detachable magazine and UV-preserving exports.
- `build_weapon_library_v3.py`: geometry, UVs, baking and exports.
- `preview_refined.py`: portable Blender files and royal material variants.

Original Claude sources were backed up in the project's `ArtSource/WeaponLibraryV3/SourceBackup_BeforeReloadRefinement`. Refined assets go into the separate `ArtSource/WeaponLibraryV3_Refined` folder; previous V3 output is preserved.

## Rebuild

Run in PowerShell from `C:\UE\Projects\CampusCombat\CollegeCampusGame`:

```powershell
$env:WEAPON_V3_OUT = 'C:\UE\Projects\CampusCombat\CollegeCampusGame\ArtSource\WeaponLibraryV3_Refined'
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --factory-startup --python Scripts/weapon_v3/build_weapon_library_v3.py -- mp40 deagle ak47 --size 1024 --quick
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --factory-startup --python Scripts/weapon_v3/preview_refined.py
```

Increase `--size` to 2048 and remove `--quick` for higher-resolution bakes. The quick option caps textures at 1024.

## Reload mesh names

| Gun | Body with magazine removed | Removable magazine |
|---|---|---|
| MP40 | SM_mp40_Body | SM_mp40_Magazine |
| AK-47 | SM_ak47_Body | SM_ak47_Magazine |
| Desert Eagle | SM_deagle_Body | SM_deagle_Magazine |

Use the FBX files under `Reload/<gun>/`. Do not use the complete `FBX/SM_<gun>.fbx` for a detachable-magazine setup: it still includes the magazine for static display compatibility.

Both pieces share the original baked texture atlas and UV1 skin projection; do not unwrap them independently. The body pivot stays at the weapon origin. The magazine pivot is at its upper insertion end. The per-gun `reload.json` gives its seated translation in centimetres in Blender's +Y-forward, +Z-up coordinates. Check axis conversion in your Unreal import settings before setting the socket position. The current V3 importer mirrors Blender X; all three magazine pivots are centered on X.

Suggested component names: `WeaponBody` and `Magazine`. Suggested socket names: `MagazineSeat` on the weapon body, and `MagazineGrip_L` on your character's left hand. These character sockets and animation clips are NOT included or created by the Blender exporter.

Epic's guides explain [static-mesh sockets](https://dev.epicgames.com/documentation/unreal-engine/using-sockets-with-static-meshes-in-unreal-engine?lang=en-US) and [skeletal-mesh sockets and attachment transform rules](https://dev.epicgames.com/documentation/unreal-engine/skeletal-mesh-sockets-in-unreal-engine).

Suggested animation events:

1. `MagGrab`: attach Magazine to the character hand with a calibrated grip offset.
2. `MagRelease`: detach keeping world transform, enable collision/physics for the dropped magazine, or hide it for a retained-mag reload.
3. `MagSpawn`: create/show the replacement at the hand socket, with physics disabled.
4. `MagInsert`: attach to MagazineSeat using the seated transform; update ammo in authoritative gameplay code.

For multiplayer, replicate reload state/ammo and magazine visibility; animation events alone are not authoritative gameplay. No character rig, reload clips, Unreal Blueprint, network behavior or collision setup has been tested by this asset pass. Fixed tube magazines and break-action guns need their own reload setup; they must not use this box-magazine workflow.

## Skins

Open `<gun>.blend` for normal materials or `<gun>_Royal.blend` for the royal variant. Images are packed into these Blender files. Hidden `<gun>_parts` holds editable source parts; hidden `<gun>_reload_exports` holds the separate meshes. Hide the complete gun before showing the split collection to avoid overlapping geometry.

UV0 is the unique baked atlas; UV1 is a side projection. Use `Textures/T_<gun>_SkinTemplate.png` to generate/paint a gun-specific skin. The side projection shares artwork between the left and right sides and stretches on surfaces viewed edge-on; use the UV0 layout for independent side/top/bottom artwork. An arbitrary AI image will not automatically fit every gun perfectly.

Normal GLB/FBX and royal Blender materials are provided separately. Complex royal shader mixing is not baked into an extra royal GLB. In Unreal, use the skin image through the existing V3 master material, UV1 pattern mode, and copy the same skin parameters to body and magazine.

The build asserts that body+magazine triangle counts equal the original and that both UV layers survive. Files still require Unreal import and in-game visual/reload testing before production use.
