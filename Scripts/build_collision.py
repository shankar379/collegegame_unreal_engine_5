"""
Campus collision for L_Match, from the three.js game's own boxes (export/unreal/college_boxes.json, written by
the game repo's scripts/tmp/export-boxes.mjs: every college box as centre / axes / size in Unreal cm).

    UnrealEditor-Cmd.exe CollegeCampusGame.uproject -run=pythonscript -script=Scripts/build_collision.py

  - SM_BoxCol_NN: every solid box as a simple box collider (BlockAll), in compact chunks
  - SM_StairRamps: the invisible stair ramps (college.js stairRamp). They block the player's capsule, so stairs
    are climbed smoothly like in the game, but ignore Visibility / Camera traces: the foot IK, vaulting and
    bullets see the real steps, not a slope floating above them
  - SM_ThinCol: thin pieces (grill rods, rails, posts, bars: two dimensions under 15 cm, the game's THIN_FOR_CAMERA).
    They block players like a wall but NOT bullets (object type PhysicsBody; the gun traces WorldStatic /
    WorldDynamic / Pawn), so you can shoot through the corridor railing design like in the three.js game
  - College_CollisionExtra: the non-box pieces (gazebo, zipline towers) as exact triangles
"""
import json
import os
import traceback

import unreal

DATA = os.environ.get("CAMPUS_DATA", r"C:\Users\durga\Desktop\tree js game\college-game\export\unreal")
REPORT = os.path.join(DATA, "build_collision_report.txt")
LEVEL = "/Game/Maps/L_Match"
DIR = "/Game/College/BoxCollision"
FOLDER = "College/Collision"
CHUNK = 1500

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
ML = unreal.MathLibrary
out = []


def log(m):
    out.append(str(m))
    with open(REPORT, "w") as f:
        f.write("\n".join(out))


def box_mesh(name, rows, shift):
    mesh = EAL.duplicate_asset("/Engine/BasicShapes/Cube", DIR + "/" + name)
    body = mesh.get_editor_property("body_setup")
    agg = body.get_editor_property("agg_geom")
    elems = []
    for r in rows:
        e = unreal.KBoxElem()
        e.set_editor_property("center", unreal.Vector(*r["c"]) + shift)
        e.set_editor_property("rotation", ML.make_rot_from_xy(unreal.Vector(*r["x"]), unreal.Vector(*r["y"])))
        e.set_editor_property("x", max(r["s"][0], 1.0))
        e.set_editor_property("y", max(r["s"][1], 1.0))
        e.set_editor_property("z", max(r["s"][2], 1.0))
        elems.append(e)
    agg.set_editor_property("box_elems", elems)
    agg.set_editor_property("sphere_elems", [])
    agg.set_editor_property("sphyl_elems", [])
    agg.set_editor_property("convex_elems", [])
    body.set_editor_property("agg_geom", agg)
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    EAL.save_loaded_asset(mesh)
    return mesh


def main():
    shift_d = json.load(open(os.path.join(DATA, "ue_shift.json")))
    shift = unreal.Vector(shift_d["x"], shift_d["y"], shift_d["z"])
    rows = json.load(open(os.path.join(DATA, "college_boxes.json")))
    THIN_CM = 15.0 * 1.1  # 0.15 m, the boxes are vertically scaled 1.1 in the game
    is_thin = lambda r: sorted(r["s"])[1] < THIN_CM
    ramps = [r for r in rows if r.get("r")]
    thin = [r for r in rows if not r.get("r") and is_thin(r)]
    solid = [r for r in rows if not r.get("r") and not is_thin(r)]
    if EAL.does_directory_exist(DIR):
        EAL.delete_directory(DIR)
    # sort by position so each chunk is a compact area (good broadphase bounds)
    solid.sort(key=lambda r: (int(r["c"][0] // 3000), int(r["c"][1] // 3000), r["c"][2]))
    meshes = [box_mesh("SM_BoxCol_%02d" % (n // CHUNK), solid[n:n + CHUNK], shift) for n in range(0, len(solid), CHUNK)]
    ramp_mesh = box_mesh("SM_StairRamps", ramps, shift)
    thin_meshes = [box_mesh("SM_ThinCol_%02d" % (n // CHUNK), thin[n:n + CHUNK], shift) for n in range(0, len(thin), CHUNK)]
    log("solid boxes %d in %d meshes, thin %d in %d meshes, stair ramps %d" % (len(solid), len(meshes), len(thin), len(thin_meshes), len(ramps)))

    t = unreal.AssetImportTask()
    t.filename = os.path.join(DATA, "college_boxes_extra.glb")
    t.destination_path = DIR + "/Extra"
    t.automated = True
    t.replace_existing = True
    t.save = True
    AT.import_asset_tasks([t])
    extra = [a for a in (unreal.load_asset(p) for p in t.imported_object_paths) if isinstance(a, unreal.StaticMesh)][0]
    unreal.EditorStaticMeshLibrary.remove_collisions(extra)
    extra.get_editor_property("body_setup").set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    EAL.save_loaded_asset(extra)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)
    removed = 0
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label()
        if lab in ("College_Collision", "College_CollisionExtra", "College_StairRamps") or lab.startswith("BoxCol_") or lab.startswith("ThinCol_"):
            eas.destroy_actor(a)
            removed += 1
    log("removed old collision actors %d" % removed)

    def place(mesh, label, location):
        act = eas.spawn_actor_from_class(unreal.StaticMeshActor, location, unreal.Rotator(0, 0, 0))
        act.static_mesh_component.set_static_mesh(mesh)
        act.set_actor_label(label)
        act.set_folder_path(FOLDER)
        c = act.static_mesh_component
        c.set_mobility(unreal.ComponentMobility.STATIC)
        c.set_collision_profile_name("BlockAll")
        c.set_editor_property("visible", False)
        c.set_editor_property("hidden_in_game", True)
        c.set_editor_property("cast_shadow", False)
        return act

    for i, mesh in enumerate(meshes):
        place(mesh, "BoxCol_%02d" % i, unreal.Vector(0, 0, 0))
    place(extra, "College_CollisionExtra", shift)
    ramp = place(ramp_mesh, "College_StairRamps", unreal.Vector(0, 0, 0))
    c = ramp.static_mesh_component
    c.set_collision_profile_name("Custom")
    c.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    c.set_collision_object_type(unreal.CollisionChannel.ECC_WORLD_STATIC)
    c.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_BLOCK)
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY, unreal.CollisionResponseType.ECR_IGNORE)
    c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    log("ramp responses: pawn %s visibility %s camera %s" % (
        c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN),
        c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY),
        c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA)))
    for i, mesh in enumerate(thin_meshes):
        t = place(mesh, "ThinCol_%02d" % i, unreal.Vector(0, 0, 0))
        tc = t.static_mesh_component
        tc.set_collision_profile_name("Custom")
        tc.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        # PhysicsBody: players (Pawn blocks PhysicsBody) and Visibility traces (vault, feet) still hit it;
        # the gun only queries WorldStatic / WorldDynamic / Pawn objects, so bullets pass between the rods
        tc.set_collision_object_type(unreal.CollisionChannel.ECC_PHYSICS_BODY)
        tc.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_BLOCK)
        tc.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
    log("placed %d solid + %d thin box actors + extra + ramps" % (len(meshes), len(thin_meshes)))
    log("saved %s" % les.save_current_level())
    EAL.save_directory(DIR, only_if_is_dirty=True, recursive=True)


try:
    main()
except Exception:
    log("ERR " + traceback.format_exc())
log("DONE")
