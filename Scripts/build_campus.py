"""
Builds the college campus natively in Unreal from the three.js game's own data (no imported model).

    1. in the three.js project:  node tools/export-unreal-recipe.mjs
       -> export/unreal/campus_recipe.json: every college box after the game's mirroring, saved part layout
          (data/college-layout.json), base lift and vertical scale, plus the other code-built pieces
          (gazebo, trees, windows, gate, globe, bases, ziplines, crates, cars, scene.json editor boxes)
    2. here:  UnrealEditor-Cmd.exe CollegeCampusGame.uproject -run=pythonscript -script=Scripts/build_campus.py

What it does in /Game/Maps/L_Match:
  - removes the old imported campus model (College_* chunks from /Game/College/Model, College/Ink outlines)
    and any earlier run of this script (folder "Campus"), then deletes those assets if nothing else uses them
  - generates the geometry with Geometry Script: each college part (front-block-L, side-wing-R, ...) becomes one
    static mesh with one section per colour, boxes / cylinders / rods / arches built from the same unit shapes
    and the same hidden-face masks the game uses; the ink outlines become thin crossed ribbons, per part
  - materials reproduce the game's look: three.js Lambert lit by its hemisphere (1.6) + sun (1.8), as an unlit
    shader (identical on every phone, no shadows, like the game)
  - places everything at the same spot the College collision uses (UE = (x, z, y) * 100 + SHIFT)
Kept as they are: College collision (College/Collision), College_Ground, CampusDoor doors (they draw their own
leaves), CampusZipline ride actors, lights, the arena boxes.
"""
import json
import math
import os
import traceback

import unreal

RECIPE = os.environ.get("CAMPUS_RECIPE", r"C:\Users\durga\Desktop\tree js game\college-game\export\unreal\campus_recipe.json")
REPORT = os.environ.get("CAMPUS_REPORT", os.path.join(os.path.dirname(RECIPE), "build_campus_report.txt"))
LEVEL = "/Game/Maps/L_Match"
ROOT = "/Game/Campus"
FOLDER = "Campus"
# where the College collision sits (see /Game/College/BoxCollision): three.js world origin in Unreal
SHIFT = unreal.Vector(15710.0, 8222.599670410156, -2.9251937866210938)
SKIP_GROUPS = {"Doors"}  # CampusDoor actors draw (and swing) the door leaves
INK = False  # pencil outlines removed (draw calls + size on phones)
INK_WIDTH = 2.0  # cm, each ribbon of an outline segment
INK_CULL = 9000.0  # cm: outlines fade out of the draw list beyond this (phones)
# Geometry Script's DynamicMesh treats (b - a) x (c - a) as pointing INTO the surface (checked with append_box)
FLIP = True

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
out = []


def log(m):
    out.append(str(m))
    with open(REPORT, "w") as f:
        f.write("\n".join(out))


# ------------------------------------------------------------------------------------------------ colour
def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_linear(h):
    return tuple(srgb_to_linear(int(h[i:i + 2], 16) / 255.0) for i in (0, 2, 4))


# ------------------------------------------------------------------------------------------------ materials
SUN = (30.0, 20.0, 50.0)  # src/main.js sun position (30, 50, 20), three.js -> Unreal axes
_l = math.sqrt(sum(v * v for v in SUN))
SUN = tuple(v / _l for v in SUN)
masters = {}
instances = {}


def _expr(m, cls, x, y):
    return MEL.create_material_expression(m, cls, x, y)


def _const3(m, rgb, x, y):
    c = _expr(m, unreal.MaterialExpressionConstant3Vector, x, y)
    c.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
    return c


def _const1(m, v, x, y):
    c = _expr(m, unreal.MaterialExpressionConstant, x, y)
    c.set_editor_property("r", v)
    return c


def _three_light(m, base):
    """base * (hemisphere(#ffffff / #d8cfb4) * 1.6 + sun(#fff6e0) * 1.8 * N.L) / pi: three.js MeshLambertMaterial"""
    n = _expr(m, unreal.MaterialExpressionVertexNormalWS, -1300, 300)
    dot = _expr(m, unreal.MaterialExpressionDotProduct, -1100, 360)
    MEL.connect_material_expressions(n, "", dot, "A")
    MEL.connect_material_expressions(_const3(m, SUN, -1300, 420), "", dot, "B")
    sat = _expr(m, unreal.MaterialExpressionSaturate, -950, 360)
    MEL.connect_material_expressions(dot, "", sat, "")
    sun = _expr(m, unreal.MaterialExpressionMultiply, -800, 400)
    MEL.connect_material_expressions(sat, "", sun, "A")
    MEL.connect_material_expressions(_const3(m, tuple(v * 1.8 / math.pi for v in hex_linear("fff6e0")), -950, 460), "", sun, "B")
    mask = _expr(m, unreal.MaterialExpressionComponentMask, -1100, 200)
    for k, on in (("r", False), ("g", False), ("b", True)):
        mask.set_editor_property(k, on)
    MEL.connect_material_expressions(n, "", mask, "")
    half = _expr(m, unreal.MaterialExpressionMultiply, -950, 200)
    MEL.connect_material_expressions(mask, "", half, "A")
    MEL.connect_material_expressions(_const1(m, 0.5, -1100, 260), "", half, "B")
    alpha = _expr(m, unreal.MaterialExpressionAdd, -820, 200)
    MEL.connect_material_expressions(half, "", alpha, "A")
    MEL.connect_material_expressions(_const1(m, 0.5, -950, 260), "", alpha, "B")
    lerp = _expr(m, unreal.MaterialExpressionLinearInterpolate, -680, 160)
    MEL.connect_material_expressions(_const3(m, tuple(v * 1.6 / math.pi for v in hex_linear("d8cfb4")), -820, 60), "", lerp, "A")
    MEL.connect_material_expressions(_const3(m, (1.6 / math.pi,) * 3, -820, 120), "", lerp, "B")
    MEL.connect_material_expressions(alpha, "", lerp, "Alpha")
    light = _expr(m, unreal.MaterialExpressionAdd, -520, 260)
    MEL.connect_material_expressions(lerp, "", light, "A")
    MEL.connect_material_expressions(sun, "", light, "B")
    final = _expr(m, unreal.MaterialExpressionMultiply, -380, 100)
    MEL.connect_material_expressions(base, "", final, "A")
    MEL.connect_material_expressions(light, "", final, "B")
    return final


def master(lit, blend, two_sided, textured):
    key = (lit, blend, two_sided, textured)
    if key in masters:
        return masters[key]
    name = "M_Campus_%s_%s%s%s" % ("Lit" if lit else "Unlit", blend.capitalize(), "_2S" if two_sided else "", "_Tex" if textured else "")
    path = "%s/Materials/%s" % (ROOT, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = AT.create_asset(name, ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", two_sided)
    m.set_editor_property("blend_mode", {"opaque": unreal.BlendMode.BLEND_OPAQUE, "translucent": unreal.BlendMode.BLEND_TRANSLUCENT, "masked": unreal.BlendMode.BLEND_MASKED}[blend])
    color = _expr(m, unreal.MaterialExpressionVectorParameter, -1900, -200)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    vc = _expr(m, unreal.MaterialExpressionVertexColor, -1900, 100)
    base = _expr(m, unreal.MaterialExpressionMultiply, -1650, -100)
    MEL.connect_material_expressions(color, "", base, "A")
    MEL.connect_material_expressions(vc, "", base, "B")
    alpha_src = None
    if textured:
        tex = _expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -1900, -450)
        tex.set_editor_property("parameter_name", "Texture")
        tex.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
        withtex = _expr(m, unreal.MaterialExpressionMultiply, -1500, -200)
        MEL.connect_material_expressions(base, "", withtex, "A")
        MEL.connect_material_expressions(tex, "RGB", withtex, "B")
        base = withtex
        alpha_src = tex
    shaded = _three_light(m, base) if lit else base
    MEL.connect_material_property(shaded, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if blend != "opaque":
        op = _expr(m, unreal.MaterialExpressionScalarParameter, -700, 600)
        op.set_editor_property("parameter_name", "Opacity")
        op.set_editor_property("default_value", 1.0)
        a = op
        if alpha_src:
            a = _expr(m, unreal.MaterialExpressionMultiply, -500, 600)
            MEL.connect_material_expressions(op, "", a, "A")
            MEL.connect_material_expressions(alpha_src, "A", a, "B")
        MEL.connect_material_property(a, "", unreal.MaterialProperty.MP_OPACITY if blend == "translucent" else unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    masters[key] = m
    return m


textures = {}


def material(color, opacity=1.0, lit=True, blend=None, two_sided=False, texture=None):
    if blend is None:
        blend = "translucent" if opacity < 0.999 else "opaque"
    key = (color, round(opacity, 3), lit, blend, two_sided, texture)
    if key in instances:
        return instances[key]
    parent = master(lit, blend, two_sided, texture is not None)
    name = "MI_Campus_%s_%s%s%s%s" % (color, int(round(opacity * 100)), "" if lit else "_U", "_2S" if two_sided else "", ("_" + texture) if texture else "")
    path = "%s/Materials/Instances/%s" % (ROOT, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mi = AT.create_asset(name, ROOT + "/Materials/Instances", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    r, g, b = hex_linear(color)
    MEL.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(r, g, b, 1))
    if blend != "opaque":
        MEL.set_material_instance_scalar_parameter_value(mi, "Opacity", opacity)
    if texture:
        MEL.set_material_instance_texture_parameter_value(mi, "Texture", textures[texture])
    EAL.save_loaded_asset(mi)
    instances[key] = mi
    return mi


def import_textures(recipe):
    import base64
    folder = os.path.join(os.path.dirname(RECIPE), "textures")
    os.makedirs(folder, exist_ok=True)
    tasks = []
    for img in recipe["images"]:
        fn = os.path.join(folder, img["id"] + ".png")
        with open(fn, "wb") as f:
            f.write(base64.b64decode(img["png"]))
        t = unreal.AssetImportTask()
        t.filename = fn
        t.destination_path = ROOT + "/Textures"
        t.destination_name = img["id"]
        t.automated = True
        t.replace_existing = True
        t.save = True
        tasks.append(t)
    if tasks:
        AT.import_asset_tasks(tasks)
    for img in recipe["images"]:
        tex = unreal.load_asset("%s/Textures/%s" % (ROOT, img["id"]))
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        EAL.save_loaded_asset(tex)
        textures[img["id"]] = tex


# ------------------------------------------------------------------------------------------------ geometry
class Section(object):
    """vertices / normals / uvs / colours / triangles for one material of one mesh"""

    def __init__(self):
        self.p, self.n, self.uv, self.c, self.t = [], [], [], [], []
        self.has_uv = False
        self.has_c = False

    def tri(self, a, b, c):
        self.t.append((a, c, b) if FLIP else (a, b, c))


class MeshBuild(object):
    def __init__(self, name):
        self.name = name
        self.sections = {}  # material -> Section
        self.tris = 0

    def section(self, mat):
        if mat not in self.sections:
            self.sections[mat] = Section()
        return self.sections[mat]


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _norm(a):
    l = math.sqrt(_dot(a, a)) or 1.0
    return (a[0] / l, a[1] / l, a[2] / l)


def add_shape(sec, shape, box, groups_hidden):
    """one recipe box: unit template (three.js, -0.5..0.5) -> o + lx*X + ly*Y + lz*Z (all Unreal cm)"""
    o, X, Y, Z = box["o"], box["x"], box["y"], box["z"]
    # normals: inverse-transpose of [X Y Z] = cofactor rows / det
    cx, cy, cz = _cross(Y, Z), _cross(Z, X), _cross(X, Y)
    det = _dot(X, cx)
    # the template is three.js CCW-outward ((b-a)x(c-a) = outward); a mirroring [X Y Z] (det < 0, the y/z swap)
    # turns that inward, so reverse it there to keep (b-a)x(c-a) outward; Section.tri then applies Unreal's convention
    keep = det > 0
    P, N, I = shape["p"], shape["n"], shape["i"]
    base = len(sec.p)
    remap = {}
    for gi, (start, count) in enumerate(shape["groups"] or [(0, len(I))]):
        if groups_hidden & (1 << gi):
            continue
        for k in range(start, start + count, 3):
            tri = []
            for idx in (I[k], I[k + 1], I[k + 2]):
                if idx not in remap:
                    lx, ly, lz = P[idx * 3], P[idx * 3 + 1], P[idx * 3 + 2]
                    sec.p.append((o[0] + lx * X[0] + ly * Y[0] + lz * Z[0], o[1] + lx * X[1] + ly * Y[1] + lz * Z[1], o[2] + lx * X[2] + ly * Y[2] + lz * Z[2]))
                    nx, ny, nz = N[idx * 3], N[idx * 3 + 1], N[idx * 3 + 2]
                    nn = (nx * cx[0] + ny * cy[0] + nz * cz[0], nx * cx[1] + ny * cy[1] + nz * cz[1], nx * cx[2] + ny * cy[2] + nz * cz[2])
                    if det < 0:
                        nn = (-nn[0], -nn[1], -nn[2])
                    sec.n.append(_norm(nn))
                    sec.uv.append((0.0, 0.0))
                    sec.c.append((1.0, 1.0, 1.0))
                    remap[idx] = len(sec.p) - 1
                tri.append(remap[idx])
            if keep:
                sec.tri(tri[0], tri[1], tri[2])
            else:
                sec.tri(tri[0], tri[2], tri[1])
    return len(sec.p) - base


def add_ribbons(sec, pts):
    """ink: each segment as two crossed quads INK_WIDTH wide (an unlit two-sided material)"""
    w = INK_WIDTH * 0.5
    for k in range(0, len(pts) - 1, 2):
        a, b = pts[k], pts[k + 1]
        d = _sub(b, a)
        if _dot(d, d) < 1e-4:
            continue
        d = _norm(d)
        ref = (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)
        s1 = _norm(_cross(d, ref))
        s2 = _norm(_cross(d, s1))
        for s in (s1, s2):
            i0 = len(sec.p)
            for p, sg in ((a, -1), (a, 1), (b, 1), (b, -1)):
                sec.p.append((p[0] + s[0] * w * sg, p[1] + s[1] * w * sg, p[2] + s[2] * w * sg))
                nrm = _cross(d, s)
                sec.n.append(nrm)
                sec.uv.append((0.0, 0.0))
                sec.c.append((1.0, 1.0, 1.0))
            sec.tri(i0, i0 + 1, i0 + 2)
            sec.tri(i0, i0 + 2, i0 + 3)


def box_edges(shape, box):
    o, X, Y, Z = box["o"], box["x"], box["y"], box["z"]
    E = shape["edges"]
    pts = []
    for k in range(0, len(E), 3):
        lx, ly, lz = E[k], E[k + 1], E[k + 2]
        pts.append((o[0] + lx * X[0] + ly * Y[0] + lz * Z[0], o[1] + lx * X[1] + ly * Y[1] + lz * Z[1], o[2] + lx * X[2] + ly * Y[2] + lz * Z[2]))
    return pts


def write_mesh(build, path):
    """MeshBuild -> StaticMesh asset (one material slot per section)"""
    dm = unreal.DynamicMesh()
    unreal.GeometryScript_Materials.enable_material_i_ds(dm)
    mats = []
    for mid, (mat, sec) in enumerate(build.sections.items()):
        if not sec.t:
            continue
        buf = unreal.GeometryScriptSimpleMeshBuffers()
        buf.vertices = [unreal.Vector(*p) for p in sec.p]
        buf.normals = [unreal.Vector(*n) for n in sec.n]
        buf.uv0 = [unreal.Vector2D(*u) for u in sec.uv]
        buf.vertex_colors = [unreal.LinearColor(c[0], c[1], c[2], 1.0) for c in sec.c]
        buf.triangles = [unreal.IntVector(*t) for t in sec.t]
        unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dm, buf, len(mats))
        mats.append(mat)
        build.tris += len(sec.t)
    if not mats:
        return None
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    opts.enable_collision = False
    opts.enable_nanite = False
    opts.enable_recompute_normals = False
    opts.enable_recompute_tangents = False
    sm, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm, path, opts)
    if sm is None:
        raise RuntimeError("could not create %s (%s)" % (path, outcome))
    slots = list(sm.get_editor_property("static_materials"))
    while len(slots) < len(mats):
        slots.append(unreal.StaticMaterial())
    for k, mat in enumerate(mats):
        slots[k] = unreal.StaticMaterial(material_interface=mat, material_slot_name="S%d" % k)
    sm.set_editor_property("static_materials", slots)
    # lightmaps are never used (unlit materials); keep the build light
    sm.set_editor_property("light_map_resolution", 4)
    EAL.save_loaded_asset(sm)
    return sm


# ------------------------------------------------------------------------------------------------ level
def clear_level(eas):
    removed = {}
    for a in eas.get_all_level_actors():
        folder = str(a.get_folder_path())
        label = a.get_actor_label()
        kill = False
        if folder == FOLDER or folder.startswith(FOLDER + "/"):
            kill = True  # an earlier run of this script
        elif folder == "College/Ink":
            kill = True  # old imported outline model
        elif folder == "College" and isinstance(a, unreal.StaticMeshActor) and label != "College_Ground":
            sm = a.static_mesh_component.static_mesh
            if sm and sm.get_path_name().startswith(("/Game/College/Model/", "/Game/College/Visual/")):
                kill = True  # old imported campus model
        if kill:
            key = folder + " " + a.get_class().get_name()
            removed[key] = removed.get(key, 0) + 1
            eas.destroy_actor(a)
    return removed


def delete_old_assets():
    gone = []
    for d in ("/Game/College/Model", "/Game/College/Ink", "/Game/College/Visual"):
        if EAL.does_directory_exist(d):
            EAL.delete_directory(d)
            gone.append(d)
    for n in ("M_Three_Opaque", "M_Three_Opaque_DS", "M_Three_Blend", "M_Three_Blend_DS"):
        p = "/Game/College/" + n
        if EAL.does_asset_exist(p):
            refs = [r for r in EAL.find_package_referencers_for_asset(p, False) if not r.startswith(("/Game/College/Model", "/Game/College/Visual", "/Game/College/Ink"))]
            if refs:
                log("kept %s (used by %s)" % (p, refs[:3]))
            else:
                EAL.delete_asset(p)
                gone.append(p)
    return gone


def place(eas, sm, label, folder, cull=0.0):
    act = eas.spawn_actor_from_class(unreal.StaticMeshActor, SHIFT, unreal.Rotator(0, 0, 0))
    act.static_mesh_component.set_static_mesh(sm)
    act.set_actor_label(label)
    act.set_folder_path(folder)
    c = act.static_mesh_component
    c.set_mobility(unreal.ComponentMobility.STATIC)
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_editor_property("can_character_step_up_on", unreal.CanBeCharacterBase.ECB_NO)
    c.set_editor_property("cast_shadow", False)
    if cull > 0:
        c.set_editor_property("ld_max_draw_distance", cull)
    return act


def main():
    with open(RECIPE) as f:
        recipe = json.load(f)
    log("recipe %s: %s" % (RECIPE, {k: v for k, v in recipe["stats"].items() if k != "skipped"}))

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)
    log("removed actors %s" % clear_level(eas))
    les.save_current_level()
    log("deleted assets %s" % delete_old_assets())
    if EAL.does_directory_exist(ROOT + "/Meshes"):
        EAL.delete_directory(ROOT + "/Meshes")

    import_textures(recipe)
    mats_by_id = {}
    for m in recipe["materials"]:
        mats_by_id[m["id"]] = m
    shapes = recipe["shapes"]
    ink = recipe["ink"]
    # ink colour: three.js draws the lines at 55 % over what is behind them; opaque here (cheap on phones),
    # pre-mixed with the paper-white walls they mostly sit on
    wall = hex_linear("f7f3e8")
    il = hex_linear(ink["color"])
    mix = [il[k] * ink["opacity"] + wall[k] * (1 - ink["opacity"]) for k in range(3)]
    ink_hex = "".join("%02x" % int(round((1.055 * (v ** (1 / 2.4)) - 0.055 if v > 0.0031308 else 12.92 * v) * 255)) for v in mix)
    ink_mat = material(ink_hex, 1.0, lit=False, two_sided=True)

    # ---- college parts (boxes)
    parts = {}
    for b in recipe["boxes"]:
        parts.setdefault(b["part"], []).append(b)
    totals = {"boxTris": 0, "inkSegments": 0, "meshes": 0, "sections": 0}
    for part, lst in sorted(parts.items()):
        build = MeshBuild(part)
        ink_build = MeshBuild(part + "_Ink")
        ink_pts = []
        for b in lst:
            shape = shapes[b["shape"]]
            hide = b.get("hide", 0) if b["shape"] == "box" else 0
            if hide != 63:
                mat = material(b["color"], b["opacity"])
                add_shape(build.section(mat), shape, b, hide)
            if INK and b["edges"]:
                ink_pts.extend(box_edges(shape, b))
        safe = part.replace("-", "_")
        sm = write_mesh(build, "%s/Meshes/SM_Campus_%s" % (ROOT, safe))
        if sm:
            place(eas, sm, "Campus_" + safe, FOLDER + "/Building")
            totals["boxTris"] += build.tris
            totals["meshes"] += 1
            totals["sections"] += len(build.sections)
        if ink_pts:
            add_ribbons(ink_build.section(ink_mat), ink_pts)
            ism = write_mesh(ink_build, "%s/Meshes/SM_CampusInk_%s" % (ROOT, safe))
            if ism:
                place(eas, ism, "CampusInk_" + safe, FOLDER + "/Ink", INK_CULL)
                totals["inkSegments"] += len(ink_pts) // 2
        log("part %-26s boxes %5d  tris %7d  sections %2d  ink segments %6d" % (part, len(lst), build.tris, len(build.sections), len(ink_pts) // 2))
        unreal.SystemLibrary.collect_garbage()

    # ---- everything else the game builds in code
    groups = {}
    for m in recipe["meshes"]:
        if m["group"] in SKIP_GROUPS:
            continue
        groups.setdefault(m["group"], []).append(m)
    for gname, lst in sorted(groups.items()):
        build = MeshBuild(gname)
        for m in lst:
            rm = mats_by_id[m["material"]]
            blend = rm["blend"] if rm["blend"] != "opaque" or rm["opacity"] < 0.999 else "opaque"
            if blend == "opaque" and rm["opacity"] < 0.999:
                blend = "translucent"
            mat = material(rm["color"], rm["opacity"], lit=not rm["unlit"], blend=blend, two_sided=rm["twoSided"], texture=rm["texture"])
            sec = build.section(mat)
            base = len(sec.p)
            P, N, U, C, I = m["p"], m["n"], m["uv"], m["c"], m["i"]
            cnt = len(P) // 3
            for k in range(cnt):
                sec.p.append((P[k * 3], P[k * 3 + 1], P[k * 3 + 2]))
                sec.n.append((N[k * 3], N[k * 3 + 1], N[k * 3 + 2]) if N else (0.0, 0.0, 1.0))
                sec.uv.append((U[k * 2], U[k * 2 + 1]) if U else (0.0, 0.0))
                sec.c.append((C[k * 3], C[k * 3 + 1], C[k * 3 + 2]) if C else (1.0, 1.0, 1.0))
            # the page already reversed three.js's winding for the y/z swap ((b-a)x(c-a) = outward in Unreal space);
            # Section.tri applies Unreal's own convention on top
            for k in range(0, len(I), 3):
                sec.tri(base + I[k], base + I[k + 1], base + I[k + 2])
        sm = write_mesh(build, "%s/Meshes/SM_Campus_%s" % (ROOT, gname))
        if sm:
            place(eas, sm, "Campus_" + gname, FOLDER + "/Props")
            totals["meshes"] += 1
            totals["sections"] += len(build.sections)
        # outlines of the code-built meshes (gazebo, editor boxes, crates)
        lines = [l for l in recipe.get("lines", []) if l["group"] == gname]
        if INK and lines:
            ink_build = MeshBuild(gname + "_Ink")
            pts = []
            for l in lines:
                P = l["p"]
                pts.extend((P[k], P[k + 1], P[k + 2]) for k in range(0, len(P), 3))
            add_ribbons(ink_build.section(ink_mat), pts)
            ism = write_mesh(ink_build, "%s/Meshes/SM_CampusInk_%s" % (ROOT, gname))
            if ism:
                place(eas, ism, "CampusInk_" + gname, FOLDER + "/Ink", INK_CULL)
                totals["inkSegments"] += len(pts) // 2
        log("group %-12s meshes %4d  tris %7d  sections %2d" % (gname, len(lst), build.tris, len(build.sections)))
        unreal.SystemLibrary.collect_garbage()

    log("totals %s, materials %d, masters %d" % (totals, len(instances), len(masters)))
    log("saved level %s" % les.save_current_level())
    EAL.save_directory(ROOT, only_if_is_dirty=True, recursive=True)


try:
    main()
except Exception:
    log("ERR " + traceback.format_exc())
log("DONE")
