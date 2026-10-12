"""
Builds "Varsity Valley", the 800 x 800 m battle-royale level, into /Game/Maps/L_VarsityValley.

    E:\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe CollegeCampusGame.uproject ^
        -run=pythonscript -script=Scripts/build_varsity_valley.py -unattended -nosplash

The shape of the map lives in Scripts/varsity_layout.py (shared with the banner renderer); this file is
only the Unreal half of it, written the way Scripts/build_campus.py writes the campus:

  - Geometry Script builds ONE merged static mesh per zone (ground, roads, hostels, market, ...), so the
    whole map is about a dozen draw calls on a phone instead of a thousand actors
  - materials are the same flat unlit look: an emissive master with a Color parameter x vertex colour x
    MPC_VarsityTime's DayNightTint, so ACampusDayNight can take the entire world to night with one
    parameter set (an unlit world cannot be darkened by a light)
  - collision is separate and simple: every solid piece becomes a KBoxElem on one invisible box-aggregate
    mesh per zone (the Scripts/build_collision.py pattern). Art and collision are the same boxes, so
    nothing is standable that does not look standable
  - the existing campus art (/Game/Campus/Meshes/SM_Campus_*) is instanced once as the college block,
    with a box collider per building part
  - lighting: a tagged directional light, sky light, fog and street lamps + one ACampusDayNight actor.
    Day is the default; "?Night=1" on the travel URL flips the preset (see Campus/CampusDayNight.cpp)
  - PlayerStarts (BR on the waiting platform, Red/Blue pads for TDM), a sky dome and a
    NavMeshBoundsVolume over the whole play space

Re-runnable: it destroys every actor in the level and rewrites /Game/VarsityValley from scratch.
"""
import math
import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import varsity_layout as L  # noqa: E402  (needs the path above)

LEVEL = "/Game/Maps/L_VarsityValley"
ROOT = "/Game/VarsityValley"
CAMPUS_MESHES = "/Game/Campus/Meshes"
FOLDER = "VarsityValley"
REPORT = os.environ.get("VARSITY_REPORT",
                        os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                     "Saved", "build_varsity_valley_report.txt"))
# Geometry Script's DynamicMesh takes (b - a) x (c - a) as pointing INTO the surface (see build_campus.py).
FLIP = True
# Which campus parts get a box collider: the buildings you bump into. Ground art, windows, palms and
# outlines stay non-blocking (the apron slab under them is what you walk on).
CAMPUS_SOLID = ("front_block_L", "front_block_R", "side_wing_L", "side_wing_R", "canteen",
                "corner_stair_tower_L", "corner_stair_tower_R", "c_base", "extra_room_bay",
                "corner_frame_L", "corner_frame_R", "Gazebo")

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
ML = unreal.MathLibrary
out = []


def log(message):
    out.append(str(message))
    try:
        with open(REPORT, "w") as handle:
            handle.write("\n".join(out))
    except OSError:
        pass
    unreal.log("[VarsityValley] %s" % message)


# --------------------------------------------------------------------------------------- colour
def srgb_to_linear(channel):
    return channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4


def hex_linear(value):
    return tuple(srgb_to_linear(int(value[index:index + 2], 16) / 255.0) for index in (0, 2, 4))


# --------------------------------------------------------------------------------------- materials
class Art(object):
    """The unlit master, the day/night parameter collection and one material instance per colour."""

    def __init__(self):
        self.tint_id = None
        self.collection = self._collection()
        self.master = self._master()
        self.instances = {}

    def _collection(self):
        path = ROOT + "/MPC_VarsityTime"
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        try:
            asset = AT.create_asset("MPC_VarsityTime", ROOT, unreal.MaterialParameterCollection,
                                    unreal.MaterialParameterCollectionFactoryNew())
            tint = unreal.CollectionVectorParameter()
            tint.set_editor_property("parameter_name", "DayNightTint")
            tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
            # A collection parameter is bound by GUID, not by name: give it one and hand the SAME guid to
            # the material node below, or the node reads zero and the whole map renders black.
            tint.set_editor_property("id", unreal.GuidLibrary.new_guid())
            asset.set_editor_property("vector_parameters", [tint])
            EAL.save_loaded_asset(asset)
            stored = list(asset.get_editor_property("vector_parameters"))
            self.tint_id = stored[0].get_editor_property("id") if stored else None
            log("MPC_VarsityTime: DayNightTint id %s" % self.tint_id)
            return asset
        except Exception as error:
            log("WARN no parameter collection (%s): the world will not dim at night" % error)
            return None

    def _master(self):
        name = "M_VV_Unlit"
        path = "%s/Materials/%s" % (ROOT, name)
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        mat = AT.create_asset(name, ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        colour = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, -200)
        colour.set_editor_property("parameter_name", "Color")
        colour.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
        vertex = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, 60)
        base = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -650, -80)
        MEL.connect_material_expressions(colour, "", base, "A")
        MEL.connect_material_expressions(vertex, "", base, "B")
        final = base
        if self.collection:
            try:
                tint = MEL.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, -900, 300)
                tint.set_editor_property("collection", self.collection)
                tint.set_editor_property("parameter_name", "DayNightTint")
                if self.tint_id:
                    tint.set_editor_property("parameter_id", self.tint_id)
                timed = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -420, 0)
                MEL.connect_material_expressions(base, "", timed, "A")
                MEL.connect_material_expressions(tint, "", timed, "B")
                final = timed
            except Exception as error:
                log("WARN DayNightTint not wired (%s)" % error)
        MEL.connect_material_property(final, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.recompile_material(mat)
        EAL.save_loaded_asset(mat)
        return mat

    def instance(self, colour):
        if colour in self.instances:
            return self.instances[colour]
        name = "MI_VV_" + colour
        path = "%s/Materials/Instances/%s" % (ROOT, name)
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        mi = AT.create_asset(name, ROOT + "/Materials/Instances", unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, self.master)
        red, green, blue = hex_linear(colour)
        MEL.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(red, green, blue, 1.0))
        EAL.save_loaded_asset(mi)
        self.instances[colour] = mi
        return mi


# --------------------------------------------------------------------------------------- geometry
class Section(object):
    """One material's worth of one zone mesh."""

    def __init__(self):
        self.p, self.n, self.uv, self.c, self.t = [], [], [], [], []

    def vert(self, position, normal, colour=(1.0, 1.0, 1.0)):
        self.p.append(position)
        self.n.append(normal)
        self.uv.append((0.0, 0.0))
        self.c.append(colour)
        return len(self.p) - 1

    def tri(self, a, b, c):
        """a, b, c wound so (b - a) x (c - a) points OUT; Unreal's convention is applied here."""
        self.t.append((a, c, b) if FLIP else (a, b, c))

    def quad(self, a, b, c, d):
        self.tri(a, b, c)
        self.tri(a, c, d)


# face table: normal, then the two in-plane axes u, v with u x v = normal
FACES = (
    ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
    ((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 1.0, 0.0)),
    ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)),
    ((0.0, -1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0)),
    ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
    ((0.0, 0.0, -1.0), (0.0, 1.0, 0.0), (1.0, 0.0, 0.0)),
)


class Zone(object):
    def __init__(self, name):
        self.name = name
        self.sections = {}
        self.boxes = []      # (centre, size, yaw) for the collision aggregate
        self.tris = 0

    def section(self, material):
        if material not in self.sections:
            self.sections[material] = Section()
        return self.sections[material]


class Builder(L.MissingEmitter):
    """The layout's emitter: boxes and prisms into merged zone meshes, plus actors for the rest."""

    def __init__(self, art):
        self.art = art
        self.zones = {}
        self.lamps = []
        self.starts = []
        self.campus_anchor = None

    def zone(self, name):
        if name not in self.zones:
            self.zones[name] = Zone(name)
        return self.zones[name]

    # ---- primitives
    def box(self, zone, colour, centre, size, yaw=0.0, solid=False):
        z = self.zone(zone)
        sec = z.section(self.art.instance(colour))
        half = (size[0] * 0.5, size[1] * 0.5, size[2] * 0.5)
        angle = math.radians(yaw)
        cos, sin = math.cos(angle), math.sin(angle)

        def place(local):
            x, y, zz = local
            return (centre[0] + x * cos - y * sin, centre[1] + x * sin + y * cos, centre[2] + zz)

        def rotate(vector):
            x, y, zz = vector
            return (x * cos - y * sin, x * sin + y * cos, zz)

        for normal, u, v in FACES:
            world_normal = rotate(normal)
            corners = []
            for su, sv in ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)):
                local = tuple(normal[k] * half[k] + su * u[k] * half[k] + sv * v[k] * half[k] for k in range(3))
                corners.append(sec.vert(place(local), world_normal))
            sec.quad(*corners)
        if solid:
            z.boxes.append((centre, size, yaw))

    def prism(self, zone, colour, centre, radius, height, sides, yaw=0.0, solid=False):
        z = self.zone(zone)
        sec = z.section(self.art.instance(colour))
        base, top = centre[2], centre[2] + height
        ring = []
        for i in range(sides):
            angle = math.radians(yaw) + 2.0 * math.pi * i / sides
            ring.append((centre[0] + radius * math.cos(angle), centre[1] + radius * math.sin(angle)))
        for i in range(sides):
            x0, y0 = ring[i]
            x1, y1 = ring[(i + 1) % sides]
            normal = (math.cos(math.atan2((y0 + y1) * 0.5 - centre[1], (x0 + x1) * 0.5 - centre[0])),
                      math.sin(math.atan2((y0 + y1) * 0.5 - centre[1], (x0 + x1) * 0.5 - centre[0])), 0.0)
            a = sec.vert((x0, y0, base), normal)
            b = sec.vert((x1, y1, base), normal)
            c = sec.vert((x1, y1, top), normal)
            d = sec.vert((x0, y0, top), normal)
            sec.quad(a, b, c, d)
        up = (0.0, 0.0, 1.0)
        hub = sec.vert((centre[0], centre[1], top), up)
        rim = [sec.vert((x, y, top), up) for x, y in ring]
        for i in range(sides):
            sec.tri(hub, rim[i], rim[(i + 1) % sides])
        if solid:
            flat = radius * 1.35
            z.boxes.append(((centre[0], centre[1], base + height * 0.5), (flat, flat, height), yaw))

    def cone(self, zone, colour, centre, radius, height, sides, yaw=0.0):
        sec = self.zone(zone).section(self.art.instance(colour))
        base = centre[2]
        tip = sec.vert((centre[0], centre[1], base + height), (0.0, 0.0, 1.0))
        ring = []
        for i in range(sides):
            angle = math.radians(yaw) + 2.0 * math.pi * i / sides
            nx, ny = math.cos(angle), math.sin(angle)
            ring.append(sec.vert((centre[0] + radius * nx, centre[1] + radius * ny, base), (nx, ny, 0.45)))
        for i in range(sides):
            sec.tri(tip, ring[i], ring[(i + 1) % sides])
        down = (0.0, 0.0, -1.0)
        hub = sec.vert((centre[0], centre[1], base), down)
        skirt = [sec.vert((centre[0] + radius * math.cos(math.radians(yaw) + 2.0 * math.pi * i / sides),
                           centre[1] + radius * math.sin(math.radians(yaw) + 2.0 * math.pi * i / sides),
                           base), down) for i in range(sides)]
        for i in range(sides):
            sec.tri(hub, skirt[(i + 1) % sides], skirt[i])

    def dome(self, zone, top, horizon, radius):
        """An inward-facing half sphere: the gradient is vertex colour, so it costs one draw call."""
        sec = self.zone(zone).section(self.art.instance(top))
        segments, rings = 18, 6
        top_rgb = hex_linear(top)
        low_rgb = hex_linear(horizon)
        grid = []
        for r in range(rings + 1):
            lat = (math.pi * 0.5) * r / rings          # 0 = zenith
            blend = (r / float(rings)) ** 0.7
            colour = tuple(top_rgb[k] + (low_rgb[k] - top_rgb[k]) * blend for k in range(3))
            row = []
            for s in range(segments):
                lon = 2.0 * math.pi * s / segments
                x = radius * math.sin(lat) * math.cos(lon)
                y = radius * math.sin(lat) * math.sin(lon)
                z = radius * math.cos(lat) * 0.55
                row.append(sec.vert((x, y, z), (0.0, 0.0, -1.0), colour))
            grid.append(row)
        for r in range(rings):
            for s in range(segments):
                s1 = (s + 1) % segments
                a, b, c, d = grid[r][s], grid[r][s1], grid[r + 1][s1], grid[r + 1][s]
                # both windings: a one-draw-call sky is not worth risking an invisible one on a
                # back-face cull, and 900 triangles of dome cost nothing
                sec.quad(a, b, c, d)
                sec.quad(d, c, b, a)

    # ---- actors
    def campus(self, anchor):
        self.campus_anchor = anchor

    def lamp(self, location):
        self.lamps.append(location)

    def start(self, kind, location, yaw):
        self.starts.append((kind, location, yaw))


# --------------------------------------------------------------------------------------- assets
def write_mesh(zone, path):
    mesh = unreal.DynamicMesh()
    unreal.GeometryScript_Materials.enable_material_i_ds(mesh)
    materials = []
    for material, sec in zone.sections.items():
        if not sec.t:
            continue
        buffers = unreal.GeometryScriptSimpleMeshBuffers()
        buffers.vertices = [unreal.Vector(*p) for p in sec.p]
        buffers.normals = [unreal.Vector(*n) for n in sec.n]
        buffers.uv0 = [unreal.Vector2D(*u) for u in sec.uv]
        buffers.vertex_colors = [unreal.LinearColor(c[0], c[1], c[2], 1.0) for c in sec.c]
        buffers.triangles = [unreal.IntVector(*t) for t in sec.t]
        unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(mesh, buffers, len(materials))
        materials.append(material)
        zone.tris += len(sec.t)
    if not materials:
        return None
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.enable_collision = False
    options.enable_nanite = False
    options.enable_recompute_normals = False
    options.enable_recompute_tangents = False
    static, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, path, options)
    if static is None:
        raise RuntimeError("could not create %s (%s)" % (path, outcome))
    slots = list(static.get_editor_property("static_materials"))
    while len(slots) < len(materials):
        slots.append(unreal.StaticMaterial())
    for index, material in enumerate(materials):
        slots[index] = unreal.StaticMaterial(material_interface=material, material_slot_name="S%d" % index)
    static.set_editor_property("static_materials", slots)
    static.set_editor_property("light_map_resolution", 4)   # unlit: no lightmap is ever built
    EAL.save_loaded_asset(static)
    return static


def collision_mesh(name, boxes):
    """Every solid piece of a zone as one box-aggregate collider (the build_collision.py pattern)."""
    path = ROOT + "/Collision/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mesh = EAL.duplicate_asset("/Engine/BasicShapes/Cube", path)
    body = mesh.get_editor_property("body_setup")
    agg = body.get_editor_property("agg_geom")
    elements = []
    for centre, size, yaw in boxes:
        element = unreal.KBoxElem()
        element.set_editor_property("center", unreal.Vector(*centre))
        element.set_editor_property("rotation", unreal.Rotator(0.0, 0.0, yaw))
        element.set_editor_property("x", max(size[0], 1.0))
        element.set_editor_property("y", max(size[1], 1.0))
        element.set_editor_property("z", max(size[2], 1.0))
        elements.append(element)
    agg.set_editor_property("box_elems", elements)
    agg.set_editor_property("sphere_elems", [])
    agg.set_editor_property("sphyl_elems", [])
    agg.set_editor_property("convex_elems", [])
    body.set_editor_property("agg_geom", agg)
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    EAL.save_loaded_asset(mesh)
    return mesh


# --------------------------------------------------------------------------------------- level
def place_mesh(eas, mesh, label, folder, location=None, collide=False, cull=0.0, visible=True):
    actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, location or unreal.Vector(0.0, 0.0, 0.0),
                                       unreal.Rotator(0.0, 0.0, 0.0))
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_editor_property("cast_shadow", False)
    if collide:
        component.set_collision_profile_name("BlockAll")
    else:
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_editor_property("can_character_step_up_on", unreal.CanBeCharacterBase.ECB_NO)
    if not visible:
        component.set_editor_property("visible", False)
        component.set_editor_property("hidden_in_game", True)
    if cull > 0.0:
        component.set_editor_property("ld_max_draw_distance", cull)
    return actor


def place_campus(eas, anchor, builder):
    """Instance the existing campus art once as the college block, with a collider per building part."""
    if not EAL.does_directory_exist(CAMPUS_MESHES):
        log("WARN %s is missing: no college block (run Scripts/build_campus.py first)" % CAMPUS_MESHES)
        return 0
    location = unreal.Vector(anchor[0], anchor[1], anchor[2])
    placed, boxes = 0, []
    for asset in EAL.list_assets(CAMPUS_MESHES, recursive=False, include_folder=False):
        path = asset.rstrip("/")
        name = path.split("/")[-1].split(".")[0]
        if not name.startswith("SM_Campus_"):
            continue  # SM_CampusInk_* outlines are off on phones
        mesh = unreal.load_asset(path)
        if not mesh:
            continue
        part = name[len("SM_Campus_"):]
        place_mesh(eas, mesh, "VV_College_" + part, FOLDER + "/CollegeBlock", location, cull=0.0)
        placed += 1
        if part in CAMPUS_SOLID:
            bounds = mesh.get_bounding_box()
            centre = (anchor[0] + (bounds.min.x + bounds.max.x) * 0.5,
                      anchor[1] + (bounds.min.y + bounds.max.y) * 0.5,
                      anchor[2] + (bounds.min.z + bounds.max.z) * 0.5)
            size = (bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z)
            boxes.append((centre, size, 0.0))
    if boxes:
        mesh = collision_mesh("SM_VVCol_CollegeBlock", boxes)
        place_mesh(eas, mesh, "VVCol_CollegeBlock", FOLDER + "/Collision", collide=True, visible=False)
    log("college block: %d campus meshes instanced at %s, %d colliders" % (placed, anchor, len(boxes)))
    return placed


def spawn_lighting(eas, builder):
    """Day values go on the actors; ACampusDayNight owns both presets and finds these by tag."""
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 12000.0),
                                     unreal.Rotator(0.0, -48.0, -35.0))
    sun.set_actor_label("VV_Sun")
    sun.set_folder_path(FOLDER + "/Lighting")
    sun.tags = ["VVSun"]
    component = sun.directional_light_component
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_intensity(3.2)
    component.set_light_color(unreal.Color(255, 246, 224))
    component.set_editor_property("cast_shadows", False)   # phones: the world is unlit anyway

    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 15000.0), unreal.Rotator(0, 0, 0))
    sky.set_actor_label("VV_SkyLight")
    sky.set_folder_path(FOLDER + "/Lighting")
    sky.tags = ["VVSky"]
    sky_component = sky.sky_light_component
    sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_component.set_intensity(1.6)
    sky_component.set_light_color(unreal.Color(216, 207, 180))
    sky_component.set_editor_property("cast_shadows", False)   # captured scene: the unlit dome IS the sky

    fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, 400.0),
                                     unreal.Rotator(0, 0, 0))
    fog.set_actor_label("VV_Fog")
    fog.set_folder_path(FOLDER + "/Lighting")
    fog.tags = ["VVFog"]
    fog_component = fog.component
    fog_component.set_editor_property("fog_density", 0.012)
    fog_component.set_editor_property("fog_height_falloff", 0.12)
    fog_component.set_editor_property("fog_inscattering_color", unreal.LinearColor(0.62, 0.74, 0.88, 1.0))
    fog_component.set_editor_property("volumetric_fog", False)

    lamps = 0
    for x, y, z in builder.lamps:
        light = eas.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z), unreal.Rotator(0, 0, 0))
        light.set_actor_label("VV_Lamp_%02d" % lamps)
        light.set_folder_path(FOLDER + "/Lighting/Lamps")
        light.tags = ["VVLamp"]
        lamp = light.point_light_component
        lamp.set_mobility(unreal.ComponentMobility.MOVABLE)
        lamp.set_intensity(4500.0)
        lamp.set_light_color(unreal.Color(255, 214, 150))
        lamp.set_attenuation_radius(1600.0)
        lamp.set_editor_property("cast_shadows", False)
        lamp.set_editor_property("visible", False)        # day is the default preset
        lamp.set_editor_property("max_draw_distance", 9000.0)
        lamps += 1
    log("lighting: sun + sky + fog + %d street lamps (off by day)" % lamps)
    return lamps


def spawn_day_night(eas):
    actor_class = unreal.load_class(None, "/Script/CollegeCampusGame.CampusDayNight")
    if not actor_class:
        log("WARN ACampusDayNight is not compiled yet: rerun this script after Build.bat to place it")
        return False
    actor = eas.spawn_actor_from_class(actor_class, unreal.Vector(0.0, 0.0, 200.0), unreal.Rotator(0, 0, 0))
    actor.set_actor_label("VV_DayNight")
    actor.set_folder_path(FOLDER + "/Lighting")
    log("placed ACampusDayNight")
    return True


def spawn_starts(eas, builder):
    counts = {}
    for kind, (x, y, z), yaw in builder.starts:
        start = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw))
        counts[kind] = counts.get(kind, 0) + 1
        start.set_actor_label("VV_Start_%s_%02d" % (kind, counts[kind]))
        start.set_folder_path(FOLDER + "/Starts")
        start.set_editor_property("player_start_tag", kind)
    log("player starts %s" % counts)
    return counts


def spawn_navmesh(eas):
    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 1000.0),
                                     unreal.Rotator(0, 0, 0))
    nav.set_actor_label("VV_NavBounds")
    nav.set_folder_path(FOLDER)
    # the spawned brush is a 200 cm cube: 400 x covers the 800 m play space, 40 z spans -3 m .. +5 m
    nav.set_actor_scale3d(unreal.Vector(400.0, 400.0, 40.0))
    bounds = nav.get_actor_bounds(False)
    log("nav bounds volume extent %s (want ~40000 x 40000)" % (bounds[1],))
    return nav


def main():
    art_level_exists = EAL.does_asset_exist(LEVEL)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if art_level_exists:
        les.load_level(LEVEL)
        killed = 0
        for actor in eas.get_all_level_actors():
            if isinstance(actor, unreal.WorldSettings) or isinstance(actor, unreal.Brush) and actor.get_actor_label() == "Brush":
                continue
            eas.destroy_actor(actor)
            killed += 1
        log("reusing %s, destroyed %d actors" % (LEVEL, killed))
    else:
        les.new_level(LEVEL)
        log("created %s" % LEVEL)
    for folder in ("/Meshes", "/Collision", "/Materials"):
        if EAL.does_directory_exist(ROOT + folder):
            EAL.delete_directory(ROOT + folder)

    art = Art()
    builder = Builder(art)
    L.build(builder)

    totals = {"zones": 0, "tris": 0, "colliders": 0, "sections": 0}
    for name, zone in sorted(builder.zones.items()):
        mesh = write_mesh(zone, "%s/Meshes/SM_VV_%s" % (ROOT, name))
        if mesh:
            cull = 0.0 if name in ("Sky", "Ground", "Hills", "Roads") else 45000.0
            place_mesh(eas, mesh, "VV_" + name, FOLDER + "/Art", cull=cull)
            totals["zones"] += 1
            totals["tris"] += zone.tris
            totals["sections"] += len(zone.sections)
        if zone.boxes:
            collider = collision_mesh("SM_VVCol_%s" % name, zone.boxes)
            place_mesh(eas, collider, "VVCol_" + name, FOLDER + "/Collision", collide=True, visible=False)
            totals["colliders"] += len(zone.boxes)
        log("zone %-12s tris %6d  sections %2d  colliders %4d" % (name, zone.tris, len(zone.sections), len(zone.boxes)))
        unreal.SystemLibrary.collect_garbage()

    if builder.campus_anchor:
        place_campus(eas, builder.campus_anchor, builder)
    spawn_lighting(eas, builder)
    spawn_day_night(eas)
    spawn_starts(eas, builder)
    spawn_navmesh(eas)

    log("totals %s, materials %d" % (totals, len(art.instances)))
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
        log("requested RebuildNavigation")
    except Exception as error:
        log("WARN navigation not rebuilt (%s): build paths once in the editor" % error)
    log("saved level %s" % les.save_current_level())
    EAL.save_directory(ROOT, only_if_is_dirty=True, recursive=True)


try:
    main()
except Exception:
    log("ERR " + traceback.format_exc())
log("DONE")
