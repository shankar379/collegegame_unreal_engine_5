"""
Drivable car art + placement for L_Match (Docs/VEHICLES_DESIGN.md, phases 0 and 8).

    UnrealEditor-Cmd.exe CollegeCampusGame.uproject -run=pythonscript -script=Scripts/build_vehicles.py

What it does:
  1. imports the engine's own sports-car STATIC meshes (no skeleton, no PhysAsset, no AnimBP - the
     kinematic ACampusVehicle wants geometry only) from
     E:/UE_5.8/Templates/TemplateResources/Standard/Vehicles/Content/SportsCar/*.FBX
     into /Game/CampusVehicles:  SM_SportsCar, SM_SportsCar_Glass, SM_SportsCar_Wheel
  2. builds four unlit material instances under the SAME masters build_campus.py made, so the car reads
     as part of the flat-shaded campus: MI_CampusCar_Body / _Glass / _Wheel / _Wreck
     (Campus/CampusVehicle.cpp loads these by path in BeginPlay)
  3. picks three team-neutral spots by reading the level's own collision - a downward trace per candidate
     for the ground, four corner probes for flatness, a box overlap for clearance, and a scoring pass that
     wants the spot equally far from both team bases and with room to drive away
  4. places three ACampusVehicle actors there and saves L_Match (nothing else in the level is touched)

Re-runnable: it deletes its own actors (label CampusCar_*) and re-imports over the same asset paths.
"""
import json
import math
import os
import traceback

import unreal

SOURCE = os.environ.get("VEHICLE_SOURCE", r"E:\UE_5.8\Templates\TemplateResources\Standard\Vehicles\Content\SportsCar")
# Same export folder Scripts/build_collision.py reads: the campus boxes and the world shift.
DATA = os.environ.get("CAMPUS_DATA", r"C:\Users\durga\Desktop\tree js game\college-game\export\unreal")
REPORT = os.environ.get("VEHICLE_REPORT", r"C:\Users\durga\Desktop\tree js game\college-game\export\unreal\build_vehicles_report.txt")
LEVEL = "/Game/Maps/L_Match"
ROOT = "/Game/CampusVehicles"
CAMPUS_MATERIALS = "/Game/Campus/Materials"
FOLDER = "Campus/Vehicles"

# The campus origin in L_Match and the two team pads (ACampusMatchGameMode::SpawnTransformFor).
ORIGIN = unreal.Vector(15710.0, 8222.599670410156, 0.0)
RED_BASE = unreal.Vector(ORIGIN.x, ORIGIN.y - 1500.0, 0.0)
BLUE_BASE = unreal.Vector(ORIGIN.x, ORIGIN.y - 14300.0, 0.0)

# Hull from CampusVehicleTuning: 460 x 190 x 150 cm, plus a margin for the clearance test.
HALF_LENGTH, HALF_WIDTH, HEIGHT = 230.0, 95.0, 150.0
HULL_CENTRE_Z = HEIGHT * 0.5
# The clearance box is the hull plus a margin, lifted 20 cm so the ground it stands on is not itself
# reported as an obstruction.
CLEAR_LIFT = 20.0
CLEAR_EXTENT = unreal.Vector(HALF_LENGTH + 40.0, HALF_WIDTH + 40.0, HULL_CENTRE_Z - 20.0)

# Search band: the middle of the campus, between the two pads, so no car is a free spawn perk.
# (local y -11500 .. -4000 of the three.js layout; x across the whole campus)
SCAN_MIN_X, SCAN_MAX_X = -9500.0, 9500.0
SCAN_MIN_Y, SCAN_MAX_Y = -11500.0, -4000.0
SCAN_STEP = 400.0
MIN_BASE_DISTANCE = 3500.0
# 32 m apart: the campus's team-neutral middle is a strip about 64 m long, so this is as far apart as
# three cars can stand while all three stay genuinely contested (60 m only fitted two).
MIN_CAR_SPACING = 3000.0
# A spot counts as team-neutral if the two bases are within 50 m of equally far away. Inside that gate
# the score is about the ground, not about shaving centimetres off the difference.
MAX_BASE_BIAS = 5000.0

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
SL = unreal.SystemLibrary
out = []


def log(message):
    out.append(str(message))
    try:
        with open(REPORT, "w") as handle:
            handle.write("\n".join(out))
    except OSError:
        pass
    unreal.log(str(message))


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


# ------------------------------------------------------------------------------------------ art
def import_mesh(name):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SOURCE, name + ".FBX")
    task.destination_path = ROOT
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    static = options.static_mesh_import_data
    static.set_editor_property("combine_meshes", True)
    static.set_editor_property("generate_lightmap_u_vs", False)
    static.set_editor_property("auto_generate_collision", False)
    options.set_editor_property("static_mesh_import_data", static)
    task.options = options
    AT.import_asset_tasks([task])
    asset = unreal.load_asset("%s/%s" % (ROOT, name))
    if asset:
        # The hull is the only collider: the art never collides and never casts a shadow.
        try:
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).remove_collisions(asset)
        except Exception:
            pass
        EAL.save_loaded_asset(asset)
    bounds = asset.get_bounding_box() if asset else None
    log("imported %-22s %s" % (name, "size %.0f x %.0f x %.0f cm" % (
        bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z) if bounds else "MISSING"))
    return asset


def srgb_to_linear(channel):
    return channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4


def hex_linear(value):
    return tuple(srgb_to_linear(int(value[index:index + 2], 16) / 255.0) for index in (0, 2, 4))


def car_material(name, colour, opacity=1.0):
    """A child of build_campus.py's own unlit master, so the car shades exactly like the campus."""
    master_path = "%s/M_Campus_Unlit_%s" % (CAMPUS_MATERIALS, "Opaque" if opacity >= 0.999 else "Translucent")
    master = unreal.load_asset(master_path)
    if not master:
        log("MISSING master %s - run Scripts/build_campus.py first" % master_path)
        return None
    path = "%s/%s" % (ROOT, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    instance = AT.create_asset(name, ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(instance, master)
    red, green, blue = hex_linear(colour)
    MEL.set_material_instance_vector_parameter_value(instance, "Color", unreal.LinearColor(red, green, blue, 1.0))
    if opacity < 0.999:
        MEL.set_material_instance_scalar_parameter_value(instance, "Opacity", opacity)
    EAL.save_loaded_asset(instance)
    log("material %-24s #%s  opacity %.2f" % (name, colour, opacity))
    return instance


# ------------------------------------------------------------------------- reading the map for spots
# WorldStatic + WorldDynamic: the object types the game's own bullet and ground traces query.
OVERLAP_TYPES = [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2]


# Ground comes from the level's own collision DATA, not from a trace: Python's FHitResult exposes no
# fields (FHitResult's UPROPERTYs are not blueprint-visible), so a trace can only say hit / miss. These
# are the exact boxes Scripts/build_collision.py turned into the SM_BoxCol_* colliders in L_Match.
GROUND_CEILING = 250.0   # cm: above this a box top is a roof or a wall cap, not a parking surface
CELL = 500.0
_grid = {}


def load_collision():
    """Index every solid collision box by its world footprint, for a fast column query."""
    shift = json.load(open(os.path.join(DATA, "ue_shift.json")))
    rows = json.load(open(os.path.join(DATA, "college_boxes.json")))
    kept = 0
    for row in rows:
        if row.get("r"):
            continue  # stair ramps: invisible slopes for capsules, not a spot to park on
        centre = row["c"]
        axis_x, axis_y = row["x"], row["y"]
        length_x = math.sqrt(sum(v * v for v in axis_x)) or 1.0
        length_y = math.sqrt(sum(v * v for v in axis_y)) or 1.0
        unit_x = [v / length_x for v in axis_x]
        unit_y = [v / length_y for v in axis_y]
        unit_z = [unit_x[1] * unit_y[2] - unit_x[2] * unit_y[1],
                  unit_x[2] * unit_y[0] - unit_x[0] * unit_y[2],
                  unit_x[0] * unit_y[1] - unit_x[1] * unit_y[0]]
        half = [value * 0.5 for value in row["s"]]
        # World AABB of the oriented box (equal to the box itself for the axis-aligned majority).
        extent = [abs(unit_x[axis]) * half[0] + abs(unit_y[axis]) * half[1] + abs(unit_z[axis]) * half[2]
                  for axis in (0, 1, 2)]
        world_x = centre[0] + shift["x"]
        world_y = centre[1] + shift["y"]
        top = centre[2] + shift["z"] + extent[2]
        if top > GROUND_CEILING:
            continue
        box = (world_x - extent[0], world_x + extent[0], world_y - extent[1], world_y + extent[1], top)
        kept += 1
        for cell_x in range(int(box[0] // CELL), int(box[1] // CELL) + 1):
            for cell_y in range(int(box[2] // CELL), int(box[3] // CELL) + 1):
                _grid.setdefault((cell_x, cell_y), []).append(box)
    log("collision: %d of %d boxes are at or below %.0f cm, in %d grid cells" % (kept, len(rows), GROUND_CEILING, len(_grid)))


def ground_at(point_x, point_y):
    """Top of the highest ground-level collision box under (x, y), or None."""
    best = None
    for box in _grid.get((int(point_x // CELL), int(point_y // CELL)), ()):
        if box[0] <= point_x <= box[1] and box[2] <= point_y <= box[3]:
            best = box[4] if best is None else max(best, box[4])
    return best


def box_blocked(centre):
    """Does a car-sized box lifted clear of the ground hit anything? (actors, or None for nothing)."""
    lifted = unreal.Vector(centre.x, centre.y, centre.z + CLEAR_LIFT)
    actors = SL.box_overlap_actors(world(), lifted, CLEAR_EXTENT, OVERLAP_TYPES, None, [])
    if isinstance(actors, tuple):
        actors = actors[1]
    return bool(actors)


def is_clear(centre, yaw):
    if box_blocked(centre):
        return False
    # Room to drive away: the same box 7 m ahead and 7 m behind must be clear too.
    forward = unreal.Vector(math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), 0.0)
    for run in (700.0, -700.0):
        if box_blocked(unreal.Vector(centre.x + forward.x * run, centre.y + forward.y * run, centre.z)):
            return False
    return True


def candidate(point_x, point_y):
    """A flat, clear, team-neutral spot at (x, y), as (score, location, yaw) - or None."""
    centre_z = ground_at(point_x, point_y)
    if centre_z is None:
        return None
    heights = []
    for offset_x, offset_y in ((HALF_LENGTH * 0.65, HALF_WIDTH), (HALF_LENGTH * 0.65, -HALF_WIDTH),
                               (-HALF_LENGTH * 0.65, HALF_WIDTH), (-HALF_LENGTH * 0.65, -HALF_WIDTH)):
        corner = ground_at(point_x + offset_x, point_y + offset_y)
        if corner is None:
            return None
        heights.append(corner)
    flatness = max(heights) - min(heights)
    if flatness > 25.0:
        return None  # a slope or a kerb edge: the car would spawn nose-in
    # ACampusVehicle's root IS the hull box, so the actor origin rides half a hull above the ground.
    location = unreal.Vector(point_x, point_y, min(heights) + HULL_CENTRE_Z)
    red = math.hypot(location.x - RED_BASE.x, location.y - RED_BASE.y)
    blue = math.hypot(location.x - BLUE_BASE.x, location.y - BLUE_BASE.y)
    if min(red, blue) < MIN_BASE_DISTANCE:
        return None  # too close to a spawn pad: a car must be contested, not a perk
    if abs(red - blue) > MAX_BASE_BIAS:
        return None  # one team would reach it first every round
    # Face the way with the most room; prefer the axis that runs between the two bases (the roads).
    best_yaw, best_clear = None, False
    for yaw in (0.0, 90.0, 180.0, 270.0):
        if is_clear(location, yaw):
            best_yaw, best_clear = yaw, True
            if yaw in (90.0, 270.0):
                break  # +-Y runs along the campus, which is where the roads go
    if not best_clear:
        return None
    # Flat ground first, then a mild pull toward dead-centre neutrality.
    score = -flatness * 20.0 - abs(red - blue) * 0.05
    return score, location, best_yaw


# If the scan comes up empty (no collision loaded, or the band is built over), fall back to three
# hand-chosen team-neutral spots near the middle of the campus. Z is approximate on purpose:
# ACampusVehicle::ProbeGround conforms the car to the real ground on its first tick.
FALLBACK = [(-4000.0, -7900.0, 90.0), (4000.0, -7900.0, 270.0), (0.0, -9600.0, 90.0)]


def pick_spots(count=3):
    found = []
    steps_x = int((SCAN_MAX_X - SCAN_MIN_X) / SCAN_STEP) + 1
    steps_y = int((SCAN_MAX_Y - SCAN_MIN_Y) / SCAN_STEP) + 1
    for index_y in range(steps_y):
        for index_x in range(steps_x):
            spot = candidate(ORIGIN.x + SCAN_MIN_X + index_x * SCAN_STEP, ORIGIN.y + SCAN_MIN_Y + index_y * SCAN_STEP)
            if spot:
                found.append(spot)
    found.sort(key=lambda entry: -entry[0])
    log("scanned %d x %d points, %d usable" % (steps_x, steps_y, len(found)))
    chosen = []
    for score, location, yaw in found:
        if all(math.hypot(location.x - other[1].x, location.y - other[1].y) >= MIN_CAR_SPACING for other in chosen):
            chosen.append((score, location, yaw))
        if len(chosen) == count:
            break
    return chosen


# ------------------------------------------------------------------------------------------ main
def main():
    log("source %s" % SOURCE)
    for name in ("SM_SportsCar", "SM_SportsCar_Glass", "SM_SportsCar_Wheel"):
        import_mesh(name)
    # Flat campus palette: a dull saturated body, pale glass, near-black tyres, charcoal wreck.
    car_material("MI_CampusCar_Body", "c2452f")
    car_material("MI_CampusCar_Glass", "cfe3ef", 0.45)
    car_material("MI_CampusCar_Wheel", "1b1c1f")
    car_material("MI_CampusCar_Wreck", "2b2b2b")
    EAL.save_directory(ROOT, only_if_is_dirty=False, recursive=True)

    editor_level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    editor_level.load_level(LEVEL)
    removed = 0
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith("CampusCar_"):
            actors.destroy_actor(actor)
            removed += 1
    log("removed old car actors %d" % removed)

    load_collision()
    # Sanity: the two spawn pads must report a ground height, or the data and the level disagree.
    log("ground at red base %s, at blue base %s"
        % (ground_at(RED_BASE.x, RED_BASE.y), ground_at(BLUE_BASE.x, BLUE_BASE.y)))

    car_class = unreal.load_class(None, "/Script/CollegeCampusGame.CampusVehicle")
    if not car_class:
        log("ERR ACampusVehicle is not loaded - build the editor target first")
        return
    spots = pick_spots(3)
    if not spots:
        log("WARN scan found nothing; falling back to the hand-chosen neutral spots")
        for offset_x, offset_y, yaw in FALLBACK:
            point_x, point_y = ORIGIN.x + offset_x, ORIGIN.y + offset_y
            ground = ground_at(point_x, point_y)
            spots.append((0.0, unreal.Vector(point_x, point_y, (ground if ground is not None else 25.0) + HULL_CENTRE_Z), yaw))
    for index, (score, location, yaw) in enumerate(spots):
        actor = actors.spawn_actor_from_class(car_class, location, unreal.Rotator(0.0, 0.0, yaw))
        actor.set_actor_label("CampusCar_%d" % (index + 1))
        actor.set_folder_path(FOLDER)
        red = math.hypot(location.x - RED_BASE.x, location.y - RED_BASE.y)
        blue = math.hypot(location.x - BLUE_BASE.x, location.y - BLUE_BASE.y)
        log("CampusCar_%d at (%.0f, %.0f, %.0f) yaw %.0f  red %.0f m  blue %.0f m  score %.1f"
            % (index + 1, location.x, location.y, location.z, yaw, red / 100.0, blue / 100.0, score))
    if len(spots) < 3:
        log("WARN only %d spots passed the clearance test - widen SCAN_* or lower MIN_CAR_SPACING" % len(spots))
    log("saved level %s" % editor_level.save_current_level())


try:
    main()
except Exception:
    log("ERR " + traceback.format_exc())
log("DONE")
