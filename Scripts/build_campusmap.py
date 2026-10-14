"""
Builds THE CAMPUS MAP, the second college battleground, into /Game/Maps/L_CampusMap.

    1. in the three.js project:  node tools/export-campusmap.mjs
       -> export/unreal/campusmap_recipe.json  (+ campusmap_minimap.png, campusmap_banner.png, which
          are copied to Content/CampusUI/MapArt as minimap_campusmap.png / map_campusmap.png)
    2. here:
       E:\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe CollegeCampusGame.uproject ^
           -run=pythonscript -script=Scripts/build_campusmap.py -unattended -nosplash

The map itself is src/world/campusmap/* in the three.js game (seven collegiate blocks round a
courtyard with a clock tower, a parking area, a sports ground, a main gate and a walled perimeter).
The recipe is written by the GAME'S OWN builder, so the layout, the colours and the collision here
are the ones a player already walks on in the browser - nothing is re-authored.

Written the way Scripts/build_campus.py (the college) and Scripts/build_varsity_valley.py (the
valley) are written, with one deliberate difference:

  - ONE MATERIAL PER KIND, NOT PER COLOUR. The three.js map draws ~4 000 repeated props as
    InstancedMesh with the colour per instance, which is why the whole map is ~30 draw calls there.
    A merged static mesh has no per-instance colour, and a material instance per colour would have
    been 111 sections. So every vertex carries a UV into a 256 x 4 PALETTE TEXTURE holding the map's
    111 colours, and the whole level draws with four materials: lit, lit two-sided, unlit and
    translucent. 15 merged meshes x ~2.5 sections = about 37 draw calls with the whole map in frame,
    and frustum culling takes most of those away from any real viewpoint.
  - the art is split BY PLACE (each building, the tower, the sports ground, the gate, the south lot
    and four quadrants of the open campus), so a player in a hostel is not paying for the far wall.

Collision is the three.js game's own declared boxes, read back out of its baked collision mesh:
1 903 KBoxElem on a handful of invisible box-aggregate meshes (the Scripts/build_collision.py
pattern), of which 75 are the SLOPED STEP RAMPS that make stairs, the podium tiers and the clock
tower walkable. Those sit on their own actor which blocks pawns but ignores Visibility and Camera
traces, so bullets, vaulting and foot IK still see the real steps under them.

Also placed: 8 PlayerStarts (4 Red / 4 Blue on the two team pads), 92 loot spots + one
ACampusLootSpawner, sun / sky / atmosphere / fog / 26 lamp point lights tagged the way
ACampusDayNight finds them, and a NavMeshBoundsVolume over the map square.

Re-runnable: it destroys every actor in the level and rewrites /Game/CampusMap from scratch.
"""
import json
import math
import os
import struct
import time
import traceback
import zlib

import unreal

RECIPE = os.environ.get("CAMPUSMAP_RECIPE",
                        r"C:\Users\durga\Desktop\tree js game\college-game\export\unreal\campusmap_recipe.json")
REPORT = os.environ.get("CAMPUSMAP_REPORT",
                        os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                     "Saved", "build_campusmap_report.txt"))
LEVEL = "/Game/Maps/L_CampusMap"
ROOT = "/Game/CampusMap"
FOLDER = "CampusMap"
# Geometry Script's DynamicMesh takes (b - a) x (c - a) as pointing INTO the surface (see
# Scripts/build_campus.py); the recipe already wound its triangles outward in Unreal space.
FLIP = True
# One collision mesh per this many boxes: small enough that the broadphase bounds stay tight.
CHUNK = 700
# Where a pawn is dropped on a team pad: the ground there is at z = 7 cm and the capsule is 95 cm
# tall from its middle, the same lift ACampusMatchGameMode uses on the college pads.
SPAWN_Z = 110.0
# Night for an unlit world: the collection ACampusDayNight loads by path, whatever level it is on.
TINT_COLLECTION = "/Game/VarsityValley/MPC_VarsityTime"
TINT_PARAMETER = "DayNightTint"

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
    unreal.log("[CampusMap] %s" % message)


def _sp(obj, prop, value):
    """set_editor_property that survives engine-version property renames (logs and continues)."""
    try:
        obj.set_editor_property(prop, value)
    except Exception as exc:
        log("skip property %s: %s" % (prop, exc))


# ------------------------------------------------------------------------------------------ colour
def srgb_to_linear(channel):
    return channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4


def hex_linear(value):
    return tuple(srgb_to_linear(int(value[index:index + 2], 16) / 255.0) for index in (0, 2, 4))


# ------------------------------------------------------------------------------------------ palette
def write_png(path, width, height, pixels):
    """A minimal RGBA PNG writer: the editor's Python has no PIL, and the palette is 1 KB of data."""
    raw = b"".join(b"\x00" + bytes(row) for row in pixels)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA, no interlace
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def build_palette(recipe):
    """The map's colours as a 256 x 4 texture; a vertex's UV is ((index + 0.5) / 256, 0.5).

    Uncompressed, sRGB, NO MIPS and NEAREST filtering: a block-compressed or filtered palette would
    bleed one colour into the next and repaint half the map."""
    width = int(recipe["paletteWidth"])
    colours = recipe["palette"]
    row = []
    for index in range(width):
        value = colours[index] if index < len(colours) else "000000"
        row.extend([int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16), 255])
    folder = os.path.dirname(RECIPE)
    png = os.path.join(folder, "campusmap_palette.png")
    write_png(png, width, 4, [row] * 4)

    task = unreal.AssetImportTask()
    task.filename = png
    task.destination_path = ROOT + "/Textures"
    task.destination_name = "T_CampusMapPalette"
    task.automated = True
    task.replace_existing = True
    task.save = True
    AT.import_asset_tasks([task])
    texture = unreal.load_asset(ROOT + "/Textures/T_CampusMapPalette")
    _sp(texture, "compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    _sp(texture, "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    _sp(texture, "filter", unreal.TextureFilter.TF_NEAREST)
    _sp(texture, "srgb", True)
    _sp(texture, "never_stream", True)
    EAL.save_loaded_asset(texture)
    log("palette %d colours -> %s" % (len(colours), texture.get_path_name()))
    return texture


# ---------------------------------------------------------------------------------------- materials
# src/main.js sun position (30, 50, 20), three.js -> Unreal axes. The same numbers
# Scripts/build_campus.py bakes into the college's materials, so both levels light identically.
SUN = (30.0, 20.0, 50.0)
_length = math.sqrt(sum(v * v for v in SUN))
SUN = tuple(v / _length for v in SUN)


def _expr(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


def _const3(material, rgb, x, y):
    node = _expr(material, unreal.MaterialExpressionConstant3Vector, x, y)
    _sp(node, "constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return node


def _const1(material, value, x, y):
    node = _expr(material, unreal.MaterialExpressionConstant, x, y)
    _sp(node, "r", value)
    return node


def three_light(material, base, base_output=""):
    """base * (hemisphere(#ffffff / #d8cfb4) * 1.6 + sun(#fff6e0) * 1.8 * N.L) / pi.

    three.js MeshLambertMaterial under the game's own two lights, as an unlit shader: identical on
    every phone, no shadow maps, exactly like the game. Lifted from Scripts/build_campus.py so the
    two levels cannot drift apart."""
    normal = _expr(material, unreal.MaterialExpressionVertexNormalWS, -1300, 300)
    dot = _expr(material, unreal.MaterialExpressionDotProduct, -1100, 360)
    MEL.connect_material_expressions(normal, "", dot, "A")
    MEL.connect_material_expressions(_const3(material, SUN, -1300, 420), "", dot, "B")
    saturate = _expr(material, unreal.MaterialExpressionSaturate, -950, 360)
    MEL.connect_material_expressions(dot, "", saturate, "")
    sun = _expr(material, unreal.MaterialExpressionMultiply, -800, 400)
    MEL.connect_material_expressions(saturate, "", sun, "A")
    MEL.connect_material_expressions(
        _const3(material, tuple(v * 1.8 / math.pi for v in hex_linear("fff6e0")), -950, 460), "", sun, "B")
    mask = _expr(material, unreal.MaterialExpressionComponentMask, -1100, 200)
    for channel, on in (("r", False), ("g", False), ("b", True)):
        _sp(mask, channel, on)
    MEL.connect_material_expressions(normal, "", mask, "")
    half = _expr(material, unreal.MaterialExpressionMultiply, -950, 200)
    MEL.connect_material_expressions(mask, "", half, "A")
    MEL.connect_material_expressions(_const1(material, 0.5, -1100, 260), "", half, "B")
    alpha = _expr(material, unreal.MaterialExpressionAdd, -820, 200)
    MEL.connect_material_expressions(half, "", alpha, "A")
    MEL.connect_material_expressions(_const1(material, 0.5, -950, 260), "", alpha, "B")
    lerp = _expr(material, unreal.MaterialExpressionLinearInterpolate, -680, 160)
    MEL.connect_material_expressions(
        _const3(material, tuple(v * 1.6 / math.pi for v in hex_linear("d8cfb4")), -820, 60), "", lerp, "A")
    MEL.connect_material_expressions(_const3(material, (1.6 / math.pi,) * 3, -820, 120), "", lerp, "B")
    MEL.connect_material_expressions(alpha, "", lerp, "Alpha")
    light = _expr(material, unreal.MaterialExpressionAdd, -520, 260)
    MEL.connect_material_expressions(lerp, "", light, "A")
    MEL.connect_material_expressions(sun, "", light, "B")
    final = _expr(material, unreal.MaterialExpressionMultiply, -380, 100)
    MEL.connect_material_expressions(base, base_output, final, "A")
    MEL.connect_material_expressions(light, "", final, "B")
    return final


def day_night_tint(material, lit_colour):
    """lit_colour * MPC_VarsityTime.DayNightTint, the way ACampusDayNight takes an unlit world to night.

    An unlit world cannot be darkened by turning a light down, so night is one vector parameter in
    /Game/VarsityValley/MPC_VarsityTime - the collection ACampusDayNight::Apply writes and the one it
    loads BY PATH, whatever level it is standing on. Wiring it here is what makes "?Night=1" do
    anything to the buildings on this map.

    THE ALPHA IS A LIVE-OR-DEAD FLAG, and it is the whole reason this is safe to add from Python.
    A collection node whose parameter id does not resolve reads (0, 0, 0, 0), and a plain multiply by
    that renders the ENTIRE MAP BLACK - which is exactly why Scripts/build_varsity_valley.py gave up
    on wiring it at all. So the tint is lerp(white, tint.rgb, tint.a): both presets set alpha 1
    (FLinearColor::White and FLinearColor(0.20, 0.24, 0.38) both carry A = 1), a dead binding reads
    alpha 0, and the worst case is a map that simply never gets darker - never a black one.

    Returns the node to hand to emissive, or `lit_colour` unchanged if the collection is not there.
    """
    collection = unreal.load_asset(TINT_COLLECTION)
    if not collection:
        log("WARN %s is missing: the world will not darken at night" % TINT_COLLECTION)
        return lit_colour
    try:
        stored = [p for p in collection.get_editor_property("vector_parameters")
                  if str(p.get_editor_property("parameter_name")) == TINT_PARAMETER]
        if not stored:
            log("WARN %s has no %s parameter: no night tint" % (TINT_COLLECTION, TINT_PARAMETER))
            return lit_colour
        node = _expr(material, unreal.MaterialExpressionCollectionParameter, -1300, 700)
        # A collection parameter is bound by GUID, not by name - and Python can neither read the
        # authored GUID off the collection nor write one onto it (both are protected). It does not
        # have to: setting Collection and then ParameterName runs the node's own PostEditChange,
        # which looks the id up in the collection exactly as the editor's drop-down does. The id is
        # read back below to say whether that worked, and the alpha guard covers it if it did not.
        _sp(node, "collection", collection)
        _sp(node, "parameter_name", TINT_PARAMETER)
        try:
            log("DayNightTint bound to %s id %s" % (TINT_COLLECTION, node.get_editor_property("parameter_id")))
        except Exception:
            # the id is not visible to Python in this engine version; it is only being read to say
            # so in the report, and a binding that did not take is caught by the alpha guard
            log("DayNightTint wired by name on %s (its id is not readable from Python)" % TINT_COLLECTION)
        rgb = _expr(material, unreal.MaterialExpressionComponentMask, -1080, 700)
        for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
            _sp(rgb, channel, on)
        MEL.connect_material_expressions(node, "", rgb, "")
        alpha = _expr(material, unreal.MaterialExpressionComponentMask, -1080, 800)
        for channel, on in (("r", False), ("g", False), ("b", False), ("a", True)):
            _sp(alpha, channel, on)
        MEL.connect_material_expressions(node, "", alpha, "")
        safe = _expr(material, unreal.MaterialExpressionLinearInterpolate, -900, 740)
        MEL.connect_material_expressions(_const3(material, (1.0, 1.0, 1.0), -1080, 900), "", safe, "A")
        MEL.connect_material_expressions(rgb, "", safe, "B")
        MEL.connect_material_expressions(alpha, "", safe, "Alpha")
        timed = _expr(material, unreal.MaterialExpressionMultiply, -240, 300)
        MEL.connect_material_expressions(lit_colour, "", timed, "A")
        MEL.connect_material_expressions(safe, "", timed, "B")
        return timed
    except Exception as error:
        log("WARN DayNightTint not wired (%s): the world will not darken at night" % error)
        return lit_colour


class Art(object):
    """One master per material KIND, and one instance per surface of the recipe."""

    def __init__(self, palette):
        self.palette = palette
        self.masters = {}
        self.instances = {}

    def master(self, lit, blend, two_sided):
        key = (lit, blend, two_sided)
        if key in self.masters:
            return self.masters[key]
        name = "M_CampusMap_%s_%s%s" % ("Lit" if lit else "Unlit", blend.capitalize(), "_2S" if two_sided else "")
        path = "%s/Materials/%s" % (ROOT, name)
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        material = AT.create_asset(name, ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
        _sp(material, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        _sp(material, "two_sided", two_sided)
        _sp(material, "blend_mode", {"opaque": unreal.BlendMode.BLEND_OPAQUE,
                                     "translucent": unreal.BlendMode.BLEND_TRANSLUCENT}[blend])
        # THE COLOUR OF EVERY SURFACE ON THE MAP, by UV. Default sampling is TexCoord0, which is the
        # palette coordinate the recipe wrote into every vertex.
        texture = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -1900, -300)
        _sp(texture, "parameter_name", "Palette")
        _sp(texture, "texture", self.palette)
        # "RGB" and not the node's default output: a texture sample's default carries alpha too.
        # The lit world takes the day / night tint; the UNLIT surfaces (lamp lenses, the team arrows)
        # deliberately do not - a street lamp that goes dark at night is not a street lamp.
        if lit:
            MEL.connect_material_property(day_night_tint(material, three_light(material, texture, "RGB")), "",
                                          unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        else:
            MEL.connect_material_property(texture, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if blend != "opaque":
            opacity = _expr(material, unreal.MaterialExpressionScalarParameter, -700, 600)
            _sp(opacity, "parameter_name", "Opacity")
            _sp(opacity, "default_value", 1.0)
            MEL.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
        MEL.recompile_material(material)
        EAL.save_loaded_asset(material)
        self.masters[key] = material
        return material

    def surface(self, surface):
        """The material instance for one recipe surface (lit / two-sided / unlit / translucent)."""
        sid = int(surface["id"])
        if sid in self.instances:
            return self.instances[sid]
        blend = "translucent" if surface["opacity"] < 0.999 else "opaque"
        parent = self.master(not surface["unlit"], blend, bool(surface["twoSided"]))
        name = "MI_CampusMap_S%d" % sid
        path = "%s/Materials/Instances/%s" % (ROOT, name)
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        mi = AT.create_asset(name, ROOT + "/Materials/Instances", unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, parent)
        MEL.set_material_instance_texture_parameter_value(mi, "Palette", self.palette)
        if blend != "opaque":
            MEL.set_material_instance_scalar_parameter_value(mi, "Opacity", float(surface["opacity"]))
        EAL.save_loaded_asset(mi)
        self.instances[sid] = mi
        return mi


# ----------------------------------------------------------------------------------------- geometry
class Section(object):
    """One material's worth of one zone mesh, straight out of the recipe's flat arrays."""

    def __init__(self, data):
        p, n, uv = data["p"], data["n"], data["uv"]
        count = len(p) // 3
        self.vertices = [unreal.Vector(p[k * 3], p[k * 3 + 1], p[k * 3 + 2]) for k in range(count)]
        self.normals = [unreal.Vector(n[k * 3], n[k * 3 + 1], n[k * 3 + 2]) for k in range(count)]
        self.uvs = [unreal.Vector2D(uv[k * 2], uv[k * 2 + 1]) for k in range(count)]
        self.colors = [unreal.LinearColor(1.0, 1.0, 1.0, 1.0)] * count
        index = data["i"]
        # the recipe wound its triangles so (b - a) x (c - a) points OUT in Unreal space; Geometry
        # Script wants the other convention, so flip here and only here
        self.triangles = [unreal.IntVector(index[k], index[k + 2], index[k + 1]) if FLIP
                          else unreal.IntVector(index[k], index[k + 1], index[k + 2])
                          for k in range(0, len(index) - 2, 3)]


def write_mesh(zone, art, surfaces, path):
    """One zone of the recipe -> one StaticMesh asset with a section per surface."""
    mesh = unreal.DynamicMesh()
    unreal.GeometryScript_Materials.enable_material_i_ds(mesh)
    materials = []
    triangles = 0
    for data in zone["sections"]:
        if not data["i"]:
            continue
        section = Section(data)
        buffers = unreal.GeometryScriptSimpleMeshBuffers()
        buffers.vertices = section.vertices
        buffers.normals = section.normals
        buffers.uv0 = section.uvs
        buffers.vertex_colors = section.colors
        buffers.triangles = section.triangles
        unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(mesh, buffers, len(materials))
        materials.append(art.surface(surfaces[int(data["surface"])]))
        triangles += len(section.triangles)
    if not materials:
        return None, 0
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
    _sp(static, "static_materials", slots)
    _sp(static, "light_map_resolution", 4)  # unlit: no lightmap is ever built
    EAL.save_loaded_asset(static)
    return static, triangles


# ---------------------------------------------------------------------------------------- collision
def collision_mesh(name, rows):
    """A run of recipe colliders as one box-aggregate mesh (the Scripts/build_collision.py pattern)."""
    path = ROOT + "/Collision/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mesh = EAL.duplicate_asset("/Engine/BasicShapes/Cube", path)
    body = mesh.get_editor_property("body_setup")
    agg = body.get_editor_property("agg_geom")
    elements = []
    for row in rows:
        element = unreal.KBoxElem()
        _sp(element, "center", unreal.Vector(*row["c"]))
        # the recipe gives the box's own X and Y axes in Unreal space, with the sizes in that order
        _sp(element, "rotation", ML.make_rot_from_xy(unreal.Vector(*row["x"]), unreal.Vector(*row["y"])))
        _sp(element, "x", max(row["s"][0], 1.0))
        _sp(element, "y", max(row["s"][1], 1.0))
        _sp(element, "z", max(row["s"][2], 1.0))
        elements.append(element)
    _sp(agg, "box_elems", elements)
    _sp(agg, "sphere_elems", [])
    _sp(agg, "sphyl_elems", [])
    _sp(agg, "convex_elems", [])
    _sp(body, "agg_geom", agg)
    _sp(body, "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    EAL.save_loaded_asset(mesh)
    return mesh


# -------------------------------------------------------------------------------------------- level
def place_mesh(eas, mesh, label, folder, collide=False, visible=True):
    actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0),
                                       unreal.Rotator(0.0, 0.0, 0.0))
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    _sp(component, "cast_shadow", False)
    if collide:
        component.set_collision_profile_name("BlockAll")
    else:
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        _sp(component, "can_character_step_up_on", unreal.CanBeCharacterBase.ECB_NO)
    if not visible:
        _sp(component, "visible", False)
        _sp(component, "hidden_in_game", True)
    return actor


def spawn_starts(eas, recipe):
    """The two team pads of the three.js map as tagged PlayerStarts (4 a side, facing each other)."""
    counts = {}
    for kind in ("Red", "Blue"):
        for index, spot in enumerate(recipe["teamSpawns"]["red" if kind == "Red" else "blue"]):
            where = unreal.Vector(spot["c"][0], spot["c"][1], SPAWN_Z)
            start = eas.spawn_actor_from_class(unreal.PlayerStart, where, unreal.Rotator(0.0, 0.0, spot["yaw"]))
            start.set_actor_label("CM_Start_%s_%02d" % (kind, index + 1))
            start.set_folder_path(FOLDER + "/Starts")
            _sp(start, "player_start_tag", kind)
            counts[kind] = counts.get(kind, 0) + 1
    log("player starts %s" % counts)
    return counts


def spawn_loot(eas, recipe):
    """Every loot spot of the three.js map as a tagged point, plus the spawner that fills them.

    ACampusLootSpawner takes actors tagged LootSpot INSIDE its own bounds, so the bounds cover the
    whole map and the count is the number of spots - the spawner shuffles and lays out a fresh set
    every round."""
    spots = recipe["lootSpots"]
    for index, spot in enumerate(spots):
        point = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(*spot["c"]), unreal.Rotator(0, 0, 0))
        point.set_actor_label("CM_Loot_%03d" % (index + 1))
        point.set_folder_path(FOLDER + "/Loot")
        point.tags = ["LootSpot"]
    spawner_class = unreal.load_class(None, "/Script/CollegeCampusGame.CampusLootSpawner")
    if not spawner_class:
        log("WARN ACampusLootSpawner is not compiled yet: rerun this script after a build to place it")
        return len(spots)
    middle = recipe["square"]["middle"]
    spawner = eas.spawn_actor_from_class(spawner_class, unreal.Vector(middle[0], middle[1], 0.0),
                                         unreal.Rotator(0, 0, 0))
    spawner.set_actor_label("CM_LootSpawner")
    spawner.set_folder_path(FOLDER + "/Loot")
    _sp(spawner, "bounds_extent", unreal.Vector(9000.0, 9000.0, 2000.0))
    _sp(spawner, "count", len(spots))
    _sp(spawner, "spot_tag", "LootSpot")
    log("loot: %d spots + spawner" % len(spots))
    return len(spots)


def spawn_lighting(eas, recipe):
    """Day values on the actors, tagged the way ACampusDayNight finds them (the valley's tags).

    The world art is unlit, so these light the characters, the sky and the fog - and give night
    somewhere to live: "?Night=1" on the travel URL flips the preset."""
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 12000.0),
                                     unreal.Rotator(0.0, -48.0, -35.0))
    sun.set_actor_label("CM_Sun")
    sun.set_folder_path(FOLDER + "/Lighting")
    sun.tags = ["VVSun"]
    component = sun.get_editor_property("light_component")
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_intensity(3.2)
    component.set_light_color(unreal.LinearColor(1.0, 0.9647, 0.8784, 1.0))
    _sp(component, "cast_shadows", False)  # phones: the world is unlit anyway
    _sp(component, "atmosphere_sun_light", True)  # the sun the SkyAtmosphere scatters

    atmosphere = eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0),
                                            unreal.Rotator(0, 0, 0))
    atmosphere.set_actor_label("CM_SkyAtmosphere")
    atmosphere.set_folder_path(FOLDER + "/Lighting")
    # (the engine's own spelling of the property, typo included)
    _sp(atmosphere.get_editor_property("sky_atmosphere_component"), "aerial_pespective_view_distance_scale", 0.6)

    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 15000.0), unreal.Rotator(0, 0, 0))
    sky.set_actor_label("CM_SkyLight")
    sky.set_folder_path(FOLDER + "/Lighting")
    sky.tags = ["VVSky"]
    sky_component = sky.get_editor_property("light_component")
    sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_component.set_intensity(1.6)
    sky_component.set_light_color(unreal.LinearColor(0.8471, 0.8118, 0.7059, 1.0))
    _sp(sky_component, "cast_shadows", False)
    _sp(sky_component, "real_time_capture", True)

    fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, 400.0),
                                     unreal.Rotator(0, 0, 0))
    fog.set_actor_label("CM_Fog")
    fog.set_folder_path(FOLDER + "/Lighting")
    fog.tags = ["VVFog"]
    fog_component = fog.component
    _sp(fog_component, "fog_density", 0.008)
    _sp(fog_component, "fog_height_falloff", 0.2)
    for prop in ("fog_inscattering_luminance", "fog_inscattering_color"):
        try:
            _sp(fog_component, prop, unreal.LinearColor(0.62, 0.74, 0.88, 1.0))
            break
        except Exception:
            pass
    _sp(fog_component, "volumetric_fog", False)

    lamps = 0
    for x, y in recipe["lamps"]:
        light = eas.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, 520.0), unreal.Rotator(0, 0, 0))
        light.set_actor_label("CM_Lamp_%02d" % lamps)
        light.set_folder_path(FOLDER + "/Lighting/Lamps")
        light.tags = ["VVLamp"]
        lamp = light.get_editor_property("light_component")
        lamp.set_mobility(unreal.ComponentMobility.MOVABLE)
        lamp.set_intensity(3200.0)
        lamp.set_light_color(unreal.LinearColor(1.0, 0.8392, 0.5882, 1.0))
        lamp.set_attenuation_radius(1200.0)
        _sp(lamp, "cast_shadows", False)
        _sp(lamp, "visible", False)  # day is the default preset
        _sp(lamp, "max_draw_distance", 7000.0)
        lamps += 1

    day_night = unreal.load_class(None, "/Script/CollegeCampusGame.CampusDayNight")
    if day_night:
        actor = eas.spawn_actor_from_class(day_night, unreal.Vector(0.0, 0.0, 200.0), unreal.Rotator(0, 0, 0))
        actor.set_actor_label("CM_DayNight")
        actor.set_folder_path(FOLDER + "/Lighting")
    else:
        log("WARN ACampusDayNight is not compiled yet: rerun this script after a build to place it")
    log("lighting: sun + atmosphere + sky light + fog + %d lamps (off by day)" % lamps)
    return lamps


def spawn_navmesh(eas, recipe):
    """Nav bounds over the WHOLE SLAB, not just the play square.

    The map square the safe zone uses is 166.65 m, but the ground slab the bots can actually walk on
    runs X -10275 .. 10500 and Y -8250 .. 8415 (the south lot and the sports ground hang off it), and
    a bot that steps off the navmesh stops dead. The volume's brush is a 200 cm cube, so the scale IS
    the extent in metres x 2."""
    low = recipe["mapBounds"]["min"]
    high = recipe["mapBounds"]["max"]
    middle = ((low[0] + high[0]) * 0.5, (low[1] + high[1]) * 0.5)
    margin = 400.0  # cm of slack so the far kerb is not on the edge of the volume
    scale = ((high[0] - low[0]) * 0.5 + margin) / 100.0, ((high[1] - low[1]) * 0.5 + margin) / 100.0
    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume,
                                     unreal.Vector(middle[0], middle[1], 800.0), unreal.Rotator(0, 0, 0))
    nav.set_actor_label("CM_NavBounds")
    nav.set_folder_path(FOLDER)
    # 30 in z spans -2.2 m .. +38 m: every floor of every block and the clock tower walk
    nav.set_actor_scale3d(unreal.Vector(round(scale[0], 2), round(scale[1], 2), 30.0))
    log("nav bounds at %s extent %s (slab %s .. %s)" % (middle, nav.get_actor_bounds(False)[1], low, high))
    return nav


def build_navigation(world):
    """Kick the navmesh build and WAIT for it, so the level is saved with the data and not mid-build."""
    try:
        unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    except Exception as error:
        log("WARN navigation not rebuilt (%s): build paths once in the editor" % error)
        return False
    deadline = time.time() + 600.0
    building = True
    while building and time.time() < deadline:
        try:
            building = unreal.NavigationSystemV1.is_navigation_being_built(world)
        except Exception as error:
            log("WARN cannot poll the navigation build (%s); saving anyway" % error)
            return False
        if building:
            time.sleep(2.0)
    log("navigation build finished: %s" % (not building))
    recast = [a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
              if a.get_class().get_name().startswith("RecastNavMesh")]
    log("nav actors: %s" % ([a.get_class().get_name() for a in recast] or "none (runtime generation only)"))
    return not building


def main():
    with open(RECIPE) as handle:
        recipe = json.load(handle)
    log("recipe %s: %s" % (RECIPE, recipe["stats"]))
    surfaces = {int(s["id"]): s for s in recipe["surfaces"]}

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if EAL.does_asset_exist(LEVEL):
        les.load_level(LEVEL)
        log("reusing %s" % LEVEL)
    else:
        les.new_level(LEVEL)
        log("created %s" % LEVEL)
    # empty it either way: a re-run starts from nothing, and a brand new level may come with
    # whatever the engine's default template put in it
    killed = 0
    for actor in eas.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings) or (isinstance(actor, unreal.Brush)
                                                       and actor.get_actor_label() == "Brush"):
            continue
        eas.destroy_actor(actor)
        killed += 1
    log("destroyed %d actors" % killed)
    for folder in ("/Meshes", "/Collision", "/Materials", "/Textures"):
        if EAL.does_directory_exist(ROOT + folder):
            EAL.delete_directory(ROOT + folder)

    art = Art(build_palette(recipe))

    totals = {"zones": 0, "triangles": 0, "sections": 0}
    for zone in sorted(recipe["zones"], key=lambda z: z["name"]):
        safe = zone["name"].replace("-", "_")
        mesh, triangles = write_mesh(zone, art, surfaces, "%s/Meshes/SM_CampusMap_%s" % (ROOT, safe))
        if mesh:
            place_mesh(eas, mesh, "CM_" + safe, FOLDER + "/Art")
            totals["zones"] += 1
            totals["triangles"] += triangles
            totals["sections"] += len(zone["sections"])
        log("zone %-16s tris %6d  sections %d" % (zone["name"], triangles, len(zone["sections"])))
        unreal.SystemLibrary.collect_garbage()

    # ---- collision: the solid boxes in compact chunks, the walkable ramps on their own actor
    solid = [row for row in recipe["colliders"] if not row["ramp"]]
    ramps = [row for row in recipe["colliders"] if row["ramp"]]
    solid.sort(key=lambda row: (int(row["c"][0] // 2000), int(row["c"][1] // 2000), row["c"][2]))
    for start in range(0, len(solid), CHUNK):
        mesh = collision_mesh("SM_CampusMapCol_%02d" % (start // CHUNK), solid[start:start + CHUNK])
        place_mesh(eas, mesh, "CMCol_%02d" % (start // CHUNK), FOLDER + "/Collision", collide=True, visible=False)
    if ramps:
        mesh = collision_mesh("SM_CampusMapRamps", ramps)
        actor = place_mesh(eas, mesh, "CM_StairRamps", FOLDER + "/Collision", collide=True, visible=False)
        component = actor.static_mesh_component
        # The invisible slopes over every flight of steps (src/world/campusmap/kit.js rampMatrix).
        # They block the player's capsule, so stairs, podium tiers and the clock tower are walked up
        # smoothly like in the game, but they ignore Visibility / Camera traces: the foot IK,
        # vaulting and bullets see the real steps, not a slope floating above them.
        component.set_collision_profile_name("Custom")
        component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        component.set_collision_object_type(unreal.CollisionChannel.ECC_WORLD_STATIC)
        component.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_BLOCK)
        component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY,
                                                    unreal.CollisionResponseType.ECR_IGNORE)
        component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA,
                                                    unreal.CollisionResponseType.ECR_IGNORE)
    log("collision: %d solid boxes in %d meshes + %d stair ramps"
        % (len(solid), (len(solid) + CHUNK - 1) // CHUNK, len(ramps)))

    spawn_starts(eas, recipe)
    spawn_loot(eas, recipe)
    spawn_lighting(eas, recipe)
    spawn_navmesh(eas, recipe)

    log("totals %s, materials %d (masters %d)" % (totals, len(art.instances), len(art.masters)))
    log("map square middle %s span %s" % (recipe["square"]["middle"], recipe["square"]["span"]))

    # The level must NOT carry a game mode override: Team Clash opens it under GlobalDefaultGameMode
    # (ACampusMatchGameMode) and battle royale picks ACampusBRGameMode with "?game=" on the travel
    # URL, exactly as L_Match and L_VarsityValley do. An override here would pin it to one of them.
    # (the world settings actor is not in EditorActorSubsystem's list: ask for the class)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings):
        current = actor.get_editor_property("default_game_mode")
        if current is not None:
            _sp(actor, "default_game_mode", None)
        log("world settings game mode override: %s" % (current.get_name() if current else "None"))

    build_navigation(world)
    log("saved level %s" % les.save_current_level())
    EAL.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
    log("actors in level: %d" % len(eas.get_all_level_actors()))


try:
    main()
except Exception:
    log("ERR " + traceback.format_exc())
log("DONE")
