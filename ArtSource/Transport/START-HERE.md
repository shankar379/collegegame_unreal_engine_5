# Campus Combat transport

Editable source: `CampusTransport.blend`. This is an original low-poly cargo aircraft inspired by the supplied silhouettes, not a reproduction of the game's proprietary mesh.

## Change the appearance

In Unreal's Content Browser, open **CampusVehicles / Transport**. Duplicate `MI_Transport_Default`, name it for your event, then open it:

1. Enable **BaseColorTexture** and choose an imported PNG/JPG.
2. Enable **Tint**. Use white for the original colors of your image, or choose an event color.
3. Adjust **Roughness** and **Metallic** if needed. Save.

Winter and festival instances are included as examples. Engine parts and the cabin keep their own materials when the exterior changes.

For a live match, a server-side Blueprint can use **Get Actor Of Class → CampusBRPlane → Set Event Livery**, passing the saved material instance. This updates the body and ramp on server and clients, including late joiners. Call it after the flight actor has spawned; the plane is destroyed after deployment, so apply your event selection to each new flight. It is not connected to a backend event calendar automatically. Use saved, cooked material instances, not transient dynamic materials, for the replicated selector.

## AI-generated textures

For a simple all-over finish, generate a square seamless painted-metal texture. Suggested prompt: “Seamless olive aircraft painted metal, subtle worn panels and rivets, flat even lighting, no perspective, no text, no logos, base color only.” Import at 1024×1024 for the mobile target, with sRGB on and mipmaps enabled.

For exact decals or artwork, use `SM_TransportBody_UV.svg` and `SM_TransportRamp_UV.svg` as UV guides, or texture-paint in Blender. An ordinary AI photograph of a plane will not align to UVs automatically. Body and ramp have independent UV layouts; the shared event material is best for all-over surface patterns. For precisely aligned ramp artwork, assign a separate ramp material in a derived implementation.

In Blender, change the image in the **Exterior_Replaceable** material's Image Texture node. The supplied panel image is packed into the blend file.

## Rear ramp

The pivot is at the bottom rear hinge, at Unreal local position (-800, 0, -150) cm. Blender frames 1–121 show a four-second opening at 30 fps. In-game the rigid ramp opens through 102 degrees over four seconds from launch, using the replicated server clock and a smooth acceleration/deceleration curve. It remains open through deployment. It has no simulated physics and does not need deforming skin weights; all of its vertices move rigidly together. This avoids a skeletal-mesh animation cost and prevents metal from bending.

## Scope and performance

The existing deployment ride, jump rules, flight route and parachute behavior remain. Passengers use the game's existing hidden-character exterior ride, now with a 55 m orbit camera centered near the aircraft so its event skin is visible. The camera blends back to the existing dive camera on exit. This does **not** add a walkable multiplayer cabin. The model includes an interior for later camera work.

Six mesh components: body, ramp and four instances of one propeller mesh. Three material sections on the body, one on each remaining component; about eight base-pass section draws before engine batching. Three generated LODs at 100%, 50% and 20% requested triangle ratios. No Nanite, mesh collision, rigid-body simulation, or plane shadow casting. One shared 1024-pixel panel texture. The server skips cosmetic propeller and ramp transforms.

The aircraft is larger than the old placeholder: roughly 26.5 m long and 40 m in span. Verify deployment camera framing, mobile lighting, distant LOD transitions, and two-client event changes in the actual map before release. Hardware bottlenecks elsewhere in the game are not resolved by this aircraft replacement.

Source geometry is 2,372 triangles including one propeller; with all four propellers displayed, LOD0 is 2,516 triangles. Final imported LOD counts and integration checks are recorded separately in `VerifiedReport.json` when the editor verification succeeds.

## Rebuild files

The project keeps its own copy under `ArtSource/Transport`. Run `Scripts/build_transport_blender.py` in Blender to regenerate the source and JSON buffers. `Scripts/import_transport.py` creates new Unreal assets and refuses to overwrite existing mesh assets; it is an initial import tool, not a routine texture-change tool. Run `Scripts/verify_transport_editor.py` in a full editor session to generate/check LODs and check the actor's material switch. These scripts do not package or install an Android build. Texture-only event changes do not require remodeling the plane.
