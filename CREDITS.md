# Credits — third-party assets

Every asset in this game that was made by somebody else is listed here, with the credit its licence
asks for. **Keep an entry as long as the asset ships** (it ships inside the Android/iOS/Windows build,
not only in this repo), and add one whenever a downloaded model, texture or sound goes in.

## 3D models

### Parachute Simple — the battle royale canopy

This work is based on
["Parachute Simple"](https://sketchfab.com/3d-models/parachute-simple-030a235d6f314a86bebc33f322c0e71c)
by [sxnneh](https://sketchfab.com/Sxnneh) licensed under
[CC-BY-4.0](http://creativecommons.org/licenses/by/4.0/).

- In the game: the canopy every diver comes down under, and the five decorative jumpers beside the
  plane (`Source/CollegeCampusGame/Campus/CampusBRPlane.cpp`,
  `Source/CollegeCampusGame/Campus/CampusBRDecorChutes.cpp`).
- Assets: `/Game/CampusVehicles/Parachute/` — `SM_Parachute`, `T_Parachute_BaseColor`,
  `M_Parachute_Canopy`, `MI_Parachute_Canopy`.
- Imported by `Scripts/import_parachute.py` from the downloaded `parachute_simple.glb`
  (glTF → one static mesh, 580 triangles, one material slot).
- Licence requirement: the author must be credited; commercial use is allowed. No changes to the
  geometry were made beyond the standard glTF → Unreal axis and unit conversion.
