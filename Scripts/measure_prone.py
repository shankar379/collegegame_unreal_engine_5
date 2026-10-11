"""Where does the body point in each prone clip? Component space (mesh faces +Y, up +Z), cm.
Writes Scripts/prone_measure.txt: per clip and time, pelvis / head / feet positions, the pelvis->head direction
(the body's long axis) and the root bone's transform. "Pointing to the sky" = body axis close to +Z."""
import math
import os
import traceback

import unreal

OUT = os.path.join(os.path.dirname(__file__), "prone_measure.txt")
CLIPS = [
    ("/Game/CampusAnims/Prone/A_Prone_Prone_Forward_Stop", [0.0, -0.02]),
    ("/Game/CampusAnims/Prone/A_Prone_Prone_Forward", [0.0, 1.0]),
    ("/Game/CampusAnims/Prone/A_Prone_Crawl_Backwards_In_Prone", [0.0, 0.5]),
    ("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_F", [0.0, 0.5, 1.0, 1.9, 2.8]),
]
lines = []
try:
    AP = unreal.AnimPoseExtensions
    opts = unreal.AnimPoseEvaluationOptions()
    W = unreal.AnimPoseSpaces.WORLD
    L = unreal.AnimPoseSpaces.LOCAL
    for path, times in CLIPS:
        seq = unreal.load_asset(path)
        if not seq:
            lines.append("%s NOT FOUND" % path)
            continue
        length = seq.get_play_length()
        lines.append("== %s  (%.2fs, root motion %s, force root lock %s)" % (path.split("/")[-1], length,
            seq.get_editor_property("enable_root_motion"), seq.get_editor_property("force_root_lock")))
        for t in times:
            t = length + t if t < 0 else t
            pose = AP.get_anim_pose_at_time(seq, t, opts)
            g = lambda b: AP.get_bone_pose(pose, b, W).translation
            root = AP.get_bone_pose(pose, "root", L)
            pel, head, fl, fr = g("pelvis"), g("head"), g("foot_l"), g("foot_r")
            ax = head - pel
            n = math.sqrt(ax.x ** 2 + ax.y ** 2 + ax.z ** 2) or 1
            lines.append("  t=%.2f pelvis (%5.0f %5.0f %5.0f) head (%5.0f %5.0f %5.0f) feet_z %3.0f/%3.0f  body axis (%+.2f %+.2f %+.2f)  root loc (%.0f %.0f %.0f) rot %s" % (
                t, pel.x, pel.y, pel.z, head.x, head.y, head.z, fl.z, fr.z, ax.x / n, ax.y / n, ax.z / n,
                root.translation.x, root.translation.y, root.translation.z, root.rotation.rotator()))
except Exception:
    lines.append("ERR " + traceback.format_exc())
open(OUT, "w").write("\n".join(lines))
