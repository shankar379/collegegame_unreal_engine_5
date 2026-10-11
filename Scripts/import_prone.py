"""
Mixamo prone / crawl clips -> the Campus mannequin (UEFN skeleton), then a measured report to pick the best ones.

    UnrealEditor-Cmd.exe CollegeCampusGame.uproject -run=pythonscript -script=Scripts/import_prone.py

  1. imports Y Bot (Mixamo character, with skin) and the clips (without skin) onto Y Bot's skeleton
  2. IK Rigs: Y Bot (auto-characterised, Mixamo is a known skeleton) and the Campus mannequin
  3. IK Retargeter Y Bot -> mannequin (auto chain map, target aligned to the source T-pose), batch retarget
  4. measures every retargeted clip: length, root travel / speed, loop seam (first vs last pose), hand / knee
     slide on the floor, body height -> prone_report.txt
Re-runnable: deletes /Game/CampusAnims/Prone first. Y Bot + the raw Mixamo assets are deleted after the retarget.
"""
import math
import os
import traceback

import unreal

DOWNLOADS = os.path.expanduser(r"~\Downloads")
CLIPS = ["Prone Forward", "Prone Forward Stop", "Crawl Forward", "Crawl Backwards In Prone", "Moving Backward In Crawl Position"]
ROOT = "/Game/CampusAnims/Prone"
SRC = ROOT + "/Mixamo"
TARGET_MESH = "/Game/CampusCharacter/SKM_CampusMannequin"
REPORT = os.path.join(os.path.dirname(__file__), "prone_report.txt")

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
out = []


def log(m):
    out.append(str(m))
    with open(REPORT, "w") as f:
        f.write("\n".join(out))


def import_fbx(path, dest, name, skeleton=None):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = dest
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    ui = unreal.FbxImportUI()
    ui.import_as_skeletal = True
    ui.import_materials = False
    ui.import_textures = False
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    if skeleton:
        ui.import_mesh = False
        ui.import_animations = True
        ui.skeleton = skeleton
        ui.anim_sequence_import_data.set_editor_property("import_bone_tracks", True)
        ui.anim_sequence_import_data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    else:
        ui.import_mesh = True
        ui.import_animations = False
    task.options = ui
    AT.import_asset_tasks([task])
    return [unreal.load_asset(p) for p in task.imported_object_paths]


def make_ik_rig(name, mesh):
    path = "%s/%s" % (ROOT, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    rig = AT.create_asset(name, ROOT, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    ctrl = unreal.IKRigController.get_controller(rig)
    ctrl.set_skeletal_mesh(mesh)
    ok_def = ctrl.apply_auto_generated_retarget_definition()
    ok_ik = ctrl.apply_auto_fbik()
    EAL.save_loaded_asset(rig)
    log("ik rig %s: auto retarget definition %s, auto FBIK %s" % (name, ok_def, ok_ik))
    return rig


def measure(seq):
    """root travel, loop seam and floor-contact slide of a retargeted clip"""
    AP = unreal.AnimPoseExtensions
    opts = unreal.AnimPoseEvaluationOptions()
    W = unreal.AnimPoseSpaces.WORLD
    length = seq.get_play_length()
    n = max(2, int(length * 30) + 1)
    bones = ["root", "pelvis", "head", "hand_l", "hand_r", "calf_l", "calf_r", "foot_l", "foot_r"]
    frames = []
    for i in range(n):
        pose = AP.get_anim_pose_at_time(seq, min(length, i / 30.0), opts)
        frames.append({b: AP.get_bone_pose(pose, b, W).translation for b in bones})
    root_travel = frames[-1]["pelvis"] - frames[0]["pelvis"]
    dist = math.hypot(root_travel.x, root_travel.y)
    speed = dist / max(length, 1e-3)
    # loop seam: the pose difference between the last and first frame, relative to the pelvis
    seam = 0.0
    for b in ("hand_l", "hand_r", "foot_l", "foot_r", "head"):
        a = frames[0][b] - frames[0]["pelvis"]
        z = frames[-1][b] - frames[-1]["pelvis"]
        seam = max(seam, math.dist((a.x, a.y, a.z), (z.x, z.y, z.z)))
    # floor contact slide: while a limb is lowest (on the floor), how much it moves along the ground per second
    slide = {}
    for b in ("hand_l", "hand_r", "calf_l", "calf_r"):
        low = min(f[b].z for f in frames)
        contact = [f for f in frames if f[b].z < low + 4.0]
        moved = 0.0
        for k in range(1, len(contact)):
            d = contact[k][b] - contact[k - 1][b]
            moved += math.hypot(d.x, d.y)
        slide[b] = round(moved / max(length, 1e-3), 1)
    pelvis_z = sum(f["pelvis"].z for f in frames) / len(frames)
    head_z = sum(f["head"].z for f in frames) / len(frames)
    return {
        "length": round(length, 2),
        "travel_cm": round(dist, 1),
        "dir": "forward" if root_travel.y > 0 else "backward",  # mannequin faces +Y
        "speed_cms": round(speed, 1),
        "loop_seam_cm": round(seam, 1),
        "slide_cms": slide,
        "pelvis_z": round(pelvis_z, 1),
        "head_z": round(head_z, 1),
    }


try:
    if EAL.does_directory_exist(ROOT):
        EAL.delete_directory(ROOT)
    # the legacy FBX importer honours FbxImportUI (skeleton to import onto, animations only)
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")

    ybot = [a for a in import_fbx(os.path.join(DOWNLOADS, "Y Bot.fbx"), SRC, "SK_YBot") if isinstance(a, unreal.SkeletalMesh)]
    if not ybot:
        raise RuntimeError("Y Bot did not import as a skeletal mesh")
    ybot = ybot[0]
    skeleton = ybot.skeleton
    log("Y Bot: %s, skeleton %s" % (ybot.get_path_name(), skeleton.get_name()))

    sources = []
    for clip in CLIPS:
        name = "M_" + clip.replace(" ", "_")
        assets = import_fbx(os.path.join(DOWNLOADS, clip + ".fbx"), SRC, name, skeleton)
        seqs = [a for a in assets if isinstance(a, unreal.AnimSequence)]
        log("imported %-36s -> %s" % (clip, [s.get_name() for s in seqs]))
        sources += seqs

    target = unreal.load_asset(TARGET_MESH)
    src_rig = make_ik_rig("IK_YBot", ybot)
    tgt_rig = make_ik_rig("IK_CampusMannequin", target)

    rtg_path = ROOT + "/RTG_YBot_To_Campus"
    retargeter = AT.create_asset("RTG_YBot_To_Campus", ROOT, unreal.IKRetargeter, unreal.IKRetargetFactory())
    rc = unreal.IKRetargeterController.get_controller(retargeter)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, tgt_rig)
    rc.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, ybot)
    rc.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, target)
    rc.add_default_ops()
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, tgt_rig)
    rc.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # Mixamo is a T-pose, the mannequin an A-pose: align the target's retarget pose to the source chains
    rc.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    EAL.save_loaded_asset(retargeter)
    log("retargeter saved: %s" % rtg_path)

    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.set_editor_property("assets_to_retarget", [EAL.find_asset_data(s.get_path_name()) for s in sources])
    inputs.set_editor_property("source_mesh", ybot)
    inputs.set_editor_property("target_mesh", target)
    inputs.set_editor_property("ik_retarget_asset", retargeter)
    inputs.set_editor_property("search", "M_")
    inputs.set_editor_property("replace", "A_Prone_")
    inputs.set_editor_property("target_path", ROOT)
    inputs.set_editor_property("include_referenced_assets", False)
    inputs.set_editor_property("overwrite_existing_files", True)
    made = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    log("retargeted %d clips" % len(made))

    log("")
    log("%-44s %6s %8s %9s %9s %9s  %s" % ("clip", "len s", "travel", "speed", "seam cm", "pelvis z", "contact slide cm/s (hands, knees)"))
    for data in made:
        seq = unreal.load_asset(str(data.package_name))
        if not isinstance(seq, unreal.AnimSequence):
            continue
        EAL.save_loaded_asset(seq)
        m = measure(seq)
        log("%-44s %6.2f %6.0f %s %6.0f cm/s %7.1f %8.1f   %s" % (seq.get_name(), m["length"], m["travel_cm"], m["dir"][:4], m["speed_cms"], m["loop_seam_cm"], m["pelvis_z"],
            ", ".join("%s %s" % (k, v) for k, v in m["slide_cms"].items())))
    # Y Bot and the raw Mixamo clips were only the translation key: remove them so nothing Mixamo stays in the
    # game. Only the retargeted A_Prone_* clips (on the Campus mannequin's skeleton) remain. The FBX files in
    # Downloads are kept, so this script can be re-run.
    for path in (SRC, ROOT + "/IK_YBot", ROOT + "/RTG_YBot_To_Campus"):
        if EAL.does_directory_exist(path):
            EAL.delete_directory(path)
        elif EAL.does_asset_exist(path):
            EAL.delete_asset(path)
    left = EAL.list_assets(ROOT, True, False)
    log("")
    log("kept in %s (Y Bot removed): %s" % (ROOT, [p.split(".")[-1] for p in left]))
    for p in left:
        a = unreal.load_asset(p)
        if isinstance(a, unreal.AnimSequence):
            log("  %s skeleton: %s" % (a.get_name(), a.get_editor_property("skeleton").get_path_name()))
except Exception:
    log("ERR " + traceback.format_exc())
log("DONE")
