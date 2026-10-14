"""
Read-back probe for the college collision in L_Match and L_VarsityValley. It never writes to a level.

    E:\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe CollegeCampusGame.uproject ^
        -run=pythonscript -script=Scripts/probe_collision.py -unattended -nosplash

WHY THIS DOES NOT USE LINE TRACES
  A -run=pythonscript commandlet loads the level but never spins up a physics scene, so
  SystemLibrary.line_trace_* returns None for every ray - including rays fired at collision that is
  provably there (checked against L_Match, whose college collision works on device). A commandlet trace
  cannot tell "no collision" from "no physics scene", so it is useless as evidence.
  Instead this probe reads the collision back out of the level: every StaticMeshActor that has collision
  enabled, its body setup, its KBoxElem list, the actor transform - and then runs the rays itself against
  those boxes. Same question, answered from the shipped data.

WHAT IT COMPARES
  The college art is the same /Game/Campus/Meshes/SM_Campus_* set in both levels, placed at a different
  origin: L_Match uses build_campus.SHIFT, the valley uses the CollegeBlock anchor from varsity_layout.
  So the same probe point in college-local space must give the SAME answer in both levels, and anything
  that differs is the bug. Probe points come from the box export college_boxes.json that the game itself
  produced, so they are real floors, real walls, real railings and the real door line, not guesses.

Writes Saved/probe_collision_report.txt.
"""
import json
import math
import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

DATA = os.environ.get("CAMPUS_DATA", r"C:\Users\durga\Desktop\tree js game\college-game\export\unreal")
REPORT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "Saved", "probe_collision_report.txt")
MATCH_SHIFT = (15710.0, 8222.599670410156, -2.9251937866210938)
VALLEY_ANCHOR = (-13000.0, -13000.0, 0.0)
CELL = 600.0          # cm, the spatial hash cell the rays march through
THIN_CM = 15.0 * 1.1  # the export THIN_FOR_CAMERA, vertically scaled 1.1 in the game

EAL = unreal.EditorAssetLibrary
ML = unreal.MathLibrary
out = []


def log(message):
    out.append(str(message))
    try:
        with open(REPORT, "w") as handle:
            handle.write("\n".join(out))
    except OSError:
        pass
    unreal.log("[probe] %s" % message)


def enum_name(value):
    return str(value).split(".")[-1].split(":")[0]


# ------------------------------------------------------------------- the level read back as box soup
class Solid(object):
    """Every KBoxElem of every collision-enabled actor in the level, in world space, in a spatial hash."""

    def __init__(self):
        self.boxes = []       # (centre, half, axis_x, axis_y, axis_z, owner_index)
        self.owners = []      # (label, object_type, ignores_visibility, blocks_pawn)
        self.grid = {}
        self.complex_meshes = []
        self.other_elems = {}

    def add_actor(self, actor):
        component = actor.static_mesh_component
        mesh = component.static_mesh
        label = actor.get_actor_label()
        try:
            if component.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
                return 0
        except Exception:
            return 0
        if not mesh:
            return 0
        try:
            body = mesh.get_editor_property("body_setup")
            agg = body.get_editor_property("agg_geom")
        except Exception:
            return 0
        ignores_visibility = (component.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY)
                              == unreal.CollisionResponseType.ECR_IGNORE)
        blocks_pawn = (component.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)
                       == unreal.CollisionResponseType.ECR_BLOCK)
        owner = len(self.owners)
        self.owners.append((label, enum_name(component.get_collision_object_type()),
                            ignores_visibility, blocks_pawn))

        flag = body.get_editor_property("collision_trace_flag")
        if flag == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            self.complex_meshes.append(label)
        for name in ("convex_elems", "sphere_elems", "sphyl_elems"):
            count = len(agg.get_editor_property(name))
            if count:
                self.other_elems[label + "/" + name] = count

        where = actor.get_actor_location()
        rotation = actor.get_actor_rotation()
        scale = actor.get_actor_scale3d()
        if abs(rotation.pitch) + abs(rotation.yaw) + abs(rotation.roll) > 0.01:
            log("  NOTE %s is rotated %s: the probe assumes unrotated collision actors" % (label, rotation))

        added = 0
        for element in agg.get_editor_property("box_elems"):
            local = element.get_editor_property("center")
            element_rotation = element.get_editor_property("rotation")
            centre = (where.x + local.x * scale.x, where.y + local.y * scale.y, where.z + local.z * scale.z)
            # FKBoxElem X/Y/Z are the FULL dimensions of the box (the engine halves them itself)
            half = (element.get_editor_property("x") * 0.5 * abs(scale.x),
                    element.get_editor_property("y") * 0.5 * abs(scale.y),
                    element.get_editor_property("z") * 0.5 * abs(scale.z))
            fwd = ML.get_forward_vector(element_rotation)
            right = ML.get_right_vector(element_rotation)
            up = ML.get_up_vector(element_rotation)
            self._insert((centre, half, (fwd.x, fwd.y, fwd.z), (right.x, right.y, right.z),
                          (up.x, up.y, up.z), owner))
            added += 1
        return added

    def _insert(self, box):
        index = len(self.boxes)
        self.boxes.append(box)
        centre, half, ax, ay, az, _owner = box
        reach = [abs(ax[k]) * half[0] + abs(ay[k]) * half[1] + abs(az[k]) * half[2] for k in range(3)]
        lo = [int(math.floor((centre[k] - reach[k]) / CELL)) for k in range(3)]
        hi = [int(math.floor((centre[k] + reach[k]) / CELL)) for k in range(3)]
        for i in range(lo[0], hi[0] + 1):
            for j in range(lo[1], hi[1] + 1):
                for k in range(lo[2], hi[2] + 1):
                    self.grid.setdefault((i, j, k), []).append(index)

    def inside(self, point, want_visible=False):
        """The first box containing point, or None. want_visible skips the stair ramps."""
        key = (int(math.floor(point[0] / CELL)), int(math.floor(point[1] / CELL)),
               int(math.floor(point[2] / CELL)))
        for index in self.grid.get(key, ()):
            centre, half, ax, ay, az, owner = self.boxes[index]
            if want_visible and self.owners[owner][2]:
                continue
            dx = point[0] - centre[0]
            dy = point[1] - centre[1]
            dz = point[2] - centre[2]
            if (abs(dx * ax[0] + dy * ax[1] + dz * ax[2]) <= half[0]
                    and abs(dx * ay[0] + dy * ay[1] + dz * ay[2]) <= half[1]
                    and abs(dx * az[0] + dy * az[1] + dz * az[2]) <= half[2]):
                return index
        return None

    def ray(self, start, end, step=2.0, want_visible=False):
        """March the segment and return (distance, point, owner label) of the first solid sample."""
        span = [end[k] - start[k] for k in range(3)]
        length = math.sqrt(sum(v * v for v in span))
        if length <= 0.0:
            return None
        direction = [v / length for v in span]
        travelled = 0.0
        while travelled <= length:
            point = tuple(start[k] + direction[k] * travelled for k in range(3))
            index = self.inside(point, want_visible)
            if index is not None:
                owner = self.owners[self.boxes[index][5]]
                return (travelled, point, owner)
            travelled += step
        return None


# --------------------------------------------------------------------------------- probe points
def unit(vector):
    length = math.sqrt(sum(c * c for c in vector))
    return [c / length for c in vector] if length > 1e-9 else None


def world_box(r):
    """(centre, world half extents) for an export row, or None if the box is not axis aligned.

    The export does NOT keep s in world x/y/z order: it gives each box its own x and y axes, and for a
    floor plate those come back as x=(1,0,0), y=(0,0,1), so s[1] is a world-Z thickness. Probing by
    s[0]/s[1]/s[2] directly is how the first version of this probe selected zero floors and zero walls.
    """
    ax, ay = unit(r["x"]), unit(r["y"])
    if not ax or not ay:
        return None
    az = unit([ax[1] * ay[2] - ax[2] * ay[1], ax[2] * ay[0] - ax[0] * ay[2], ax[0] * ay[1] - ax[1] * ay[0]])
    if not az:
        return None
    axes = (ax, ay, az)
    if any(max(abs(c) for c in a) < 0.999 for a in axes):
        return None
    half = (r["s"][0] * 0.5, r["s"][1] * 0.5, r["s"][2] * 0.5)
    world = [0.0, 0.0, 0.0]
    for index, a in enumerate(axes):
        for k in range(3):
            world[k] += abs(a[k]) * half[index]
    return (tuple(float(v) for v in r["c"]), tuple(world))


def pick_probes():
    rows = json.load(open(os.path.join(DATA, "college_boxes.json")))
    aligned = []
    for r in rows:
        if r.get("r"):
            continue
        box = world_box(r)
        if box:
            aligned.append(box)
    log("box export: %d rows, %d axis-aligned non-ramp, %d ramps"
        % (len(rows), len(aligned), len([r for r in rows if r.get("r")])))

    # FLOORS: the widest flat slab per storey height. Stand in the middle of it.
    floors = [b for b in aligned if b[1][2] < 35.0 and b[1][0] > 150.0 and b[1][1] > 150.0]
    by_storey = {}
    for centre, half in sorted(floors, key=lambda b: -(b[1][0] * b[1][1])):
        by_storey.setdefault(int(round(centre[2] / 100.0)), (centre, half))
    floor_probes = [("floor z%d" % round(centre[2] + half[2]),
                     (centre[0], centre[1], centre[2] + half[2]))
                    for _k, (centre, half) in sorted(by_storey.items())][:10]

    # WALLS: tall slabs, crossed horizontally 120 cm above their own base
    walls = [b for b in aligned if b[1][2] > 125.0 and min(b[1][0], b[1][1]) < 30.0
             and max(b[1][0], b[1][1]) > 200.0]
    walls.sort(key=lambda b: -(max(b[1][0], b[1][1]) * b[1][2]))
    wall_probes = []
    for centre, half in walls[:8]:
        z = centre[2] + 120.0
        if half[0] < half[1]:
            wall_probes.append(("wall x@%d,%d" % (centre[0], centre[1]),
                                (centre[0] - 300.0, centre[1], z), (centre[0] + 300.0, centre[1], z),
                                300.0 - half[0]))
        else:
            wall_probes.append(("wall y@%d,%d" % (centre[0], centre[1]),
                                (centre[0], centre[1] - 300.0, z), (centre[0], centre[1] + 300.0, z),
                                300.0 - half[1]))

    # THIN pieces: railings, grill rods, poles. Block a player, let a bullet through.
    thins = [b for b in aligned if sorted(b[1])[1] < THIN_CM * 0.5 and b[1][2] > 30.0]
    thins.sort(key=lambda b: -b[1][2])
    thin_probes = [("thin @%d,%d" % (centre[0], centre[1]),
                    (centre[0] - 200.0, centre[1], centre[2]), (centre[0] + 200.0, centre[1], centre[2]))
                   for centre, half in thins[:5]]

    # RAMPS: the invisible stair slopes. Solid for the capsule, ignored by Visibility.
    ramps = [r for r in rows if r.get("r")]
    ramps.sort(key=lambda r: -(r["s"][0] * r["s"][2]))
    ramp_probes = [("ramp @%d,%d,%d" % (r["c"][0], r["c"][1], r["c"][2]),
                    (r["c"][0], r["c"][1], r["c"][2] + 300.0), (r["c"][0], r["c"][1], r["c"][2] - 300.0))
                   for r in ramps[:6]]
    return floor_probes, wall_probes, thin_probes, ramp_probes


def door_spans(solid, origin, y_local, z_local, x0, x1, step=20.0):
    """March across a wall at waist height and report the OPEN runs: the doorways."""
    spans, start = [], None
    x = x0
    while x <= x1:
        px, py, pz = origin[0] + x, origin[1] + y_local, origin[2] + z_local
        blocked = solid.ray((px, py - 120.0, pz), (px, py + 120.0, pz)) is not None
        if blocked:
            if start is not None:
                spans.append((start, x - step))
                start = None
        elif start is None:
            start = x
        x += step
    if start is not None:
        spans.append((start, x1))
    return [(a, b) for a, b in spans if b - a >= 60.0]


# ---------------------------------------------------------------------------------------- level
def dump_level(level, origin, probes):
    floor_probes, wall_probes, thin_probes, ramp_probes = probes
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(level)
    log("")
    log("=" * 110)
    log("%s   college origin %s" % (level, origin))
    log("=" * 110)

    actors = eas.get_all_level_actors()
    solid = Solid()
    rows, meshes = [], 0
    for actor in actors:
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        meshes += 1
        added = solid.add_actor(actor)
        label = actor.get_actor_label()
        component = actor.static_mesh_component
        mesh = component.static_mesh
        if not any(k in label for k in ("Col", "ollege", "Ramp", "Campus_")):
            continue
        flag = "-"
        if mesh:
            try:
                flag = enum_name(mesh.get_editor_property("body_setup")
                                 .get_editor_property("collision_trace_flag"))
            except Exception:
                pass
        rows.append((label, mesh.get_name() if mesh else "<<NONE>>",
                     enum_name(component.get_collision_enabled()),
                     str(component.get_collision_profile_name()),
                     enum_name(component.get_collision_object_type()),
                     added, flag,
                     enum_name(component.get_collision_response_to_channel(
                         unreal.CollisionChannel.ECC_VISIBILITY))))

    log("static mesh actors %d of %d total; collision boxes read back: %d" % (meshes, len(actors), len(solid.boxes)))
    # the world AABB of all the college collision, which must cover the college ART bounds below
    college = [1e9] * 3 + [-1e9] * 3
    college_boxes = 0
    for centre, half, ax, ay, az, owner in solid.boxes:
        label = solid.owners[owner][0]
        if not (label.startswith(("BoxCol_", "ThinCol_", "VVColCollege_")) or "StairRamps" in label):
            continue
        college_boxes += 1
        reach = [abs(ax[k]) * half[0] + abs(ay[k]) * half[1] + abs(az[k]) * half[2] for k in range(3)]
        for k in range(3):
            college[k] = min(college[k], centre[k] - reach[k])
            college[k + 3] = max(college[k + 3], centre[k] + reach[k])
    if college_boxes:
        log("college COLLISION boxes %d, world bounds %s .. %s"
            % (college_boxes, [round(v) for v in college[:3]], [round(v) for v in college[3:]]))
    if solid.complex_meshes:
        log("complex-as-simple (triangle) collision on: %s" % ", ".join(solid.complex_meshes))
    if solid.other_elems:
        log("non-box collision elements: %s" % solid.other_elems)
    log("")
    log("%-27s %-31s %-11s %-10s %-17s %6s %-26s %s"
        % ("label", "mesh", "collision", "profile", "object type", "boxes", "trace flag", "vis"))
    for row in sorted(rows, key=lambda r: r[0]):
        log("%-27s %-31s %-11s %-10s %-17s %6d %-26s %s"
            % (row[0][:27], row[1][:31], row[2], row[3][:10], row[4], row[5], row[6], row[7]))

    low, high = [1e9] * 3, [-1e9] * 3
    for actor in actors:
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        label = actor.get_actor_label()
        mesh = actor.static_mesh_component.static_mesh
        if not mesh or not label.startswith(("VV_College_", "Campus_")):
            continue
        b = mesh.get_bounding_box()
        for i, (a, c) in enumerate(((b.min.x, b.max.x), (b.min.y, b.max.y), (b.min.z, b.max.z))):
            low[i] = min(low[i], a)
            high[i] = max(high[i], c)
    if low[0] < 1e8:
        log("college ART local bounds %s .. %s" % ([round(v) for v in low], [round(v) for v in high]))
        log("college ART world bounds %s .. %s"
            % ([round(low[i] + origin[i]) for i in range(3)], [round(high[i] + origin[i]) for i in range(3)]))

    log("")
    log("-- FLOORS: ray straight down from 300 cm above the slab top. Want a hit at drop ~300.")
    for name, point in floor_probes:
        p = tuple(point[k] + origin[k] for k in range(3))
        hit = solid.ray((p[0], p[1], p[2] + 300.0), (p[0], p[1], p[2] - 200.0))
        if hit:
            log("  %-16s floor z %9.1f | drop %6.1f cm -> z %9.1f  %s"
                % (name, p[2], hit[0], hit[1][2], hit[2][0]))
        else:
            log("  %-16s floor z %9.1f | NO FLOOR  <-- the player falls through this floor" % (name, p[2]))

    log("")
    log("-- WALLS: ray across the wall from 300 cm out. Want a hit at about the want-distance.")
    for name, a, b, want in wall_probes:
        pa = tuple(a[k] + origin[k] for k in range(3))
        pb = tuple(b[k] + origin[k] for k in range(3))
        hit = solid.ray(pa, pb)
        if hit:
            log("  %-22s want dist %6.1f | HIT dist %6.1f  %s" % (name, want, hit[0], hit[2][0]))
        else:
            log("  %-22s want dist %6.1f | NO HIT  <-- the player walks through this wall" % (name, want))

    log("")
    log("-- THIN pieces: railings, rods and poles. Solid for a player, see-through for a bullet.")
    for name, a, b in thin_probes:
        pa = tuple(a[k] + origin[k] for k in range(3))
        pb = tuple(b[k] + origin[k] for k in range(3))
        hit = solid.ray(pa, pb, step=1.0)
        log("  %-22s %s" % (name, ("HIT dist %6.1f  %s object %s  ignores Visibility %s"
                                   % (hit[0], hit[2][0], hit[2][1], hit[2][2])) if hit
                            else "NO HIT  <-- the player walks through this railing"))

    log("")
    log("-- STAIR RAMPS: must be solid, and must ignore Visibility so bullets see the real steps.")
    for name, a, b in ramp_probes:
        pa = tuple(a[k] + origin[k] for k in range(3))
        pb = tuple(b[k] + origin[k] for k in range(3))
        hit = solid.ray(pa, pb)
        visible = solid.ray(pa, pb, want_visible=True)
        log("  %-24s %-44s | visible-only ray hits %s"
            % (name, ("solid at drop %6.1f  %s" % (hit[0], hit[2][0])) if hit else "NOT SOLID  <-- stairs unwalkable",
               (visible[2][0] if visible else "nothing (correct: bullets pass)")))

    log("")
    log("-- DOORWAYS: march a wall at waist height. The OPEN runs are the doors.")
    for y_local, z_local, name in ((-11330.0, 160.0, "front wall y=-11330"),
                                   (-10590.0, 160.0, "inner wall y=-10590"),
                                   (-9270.0, 160.0, "courtyard wall y=-9270")):
        spans = door_spans(solid, origin, y_local, z_local, -3000.0, 3000.0)
        log("  %-24s open runs: %s" % (name, [("%.0f..%.0f" % s) for s in spans] or "NONE, sealed"))
    return solid


def main():
    probes = pick_probes()
    log("probe points: %d floors, %d walls, %d thin, %d ramps"
        % (len(probes[0]), len(probes[1]), len(probes[2]), len(probes[3])))
    dump_level("/Game/Maps/L_Match", MATCH_SHIFT, probes)
    dump_level("/Game/Maps/L_VarsityValley", VALLEY_ANCHOR, probes)


try:
    main()
except Exception:
    log("ERR " + traceback.format_exc())
log("DONE")
