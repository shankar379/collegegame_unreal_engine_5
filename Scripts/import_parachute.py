"""
The battle royale canopy: imports the downloaded "Parachute Simple" model as ONE static mesh with its
base-colour texture and a mobile material, for Campus/CampusBRPlane.cpp (ACampusBRChute) and the five
decorative jumpers in Campus/CampusBRDecorChutes.cpp.

    set PARACHUTE_GLB=C:\\Users\\durga\\Downloads\\parachute_simple.glb
    UnrealEditor-Cmd.exe CollegeCampusGame.uproject -run=pythonscript -script=Scripts/import_parachute.py

Credit (CC-BY-4.0, see CREDITS.md): "Parachute Simple" by sxnneh.

Why the geometry is read here instead of handed to the glTF importer: the same reason build_campus.py /
build_guns.py build their meshes with Geometry Script. The importer decides the asset name, the axis
conversion, the material set and the pivot for us; this script states all four, so SM_Parachute is
deterministic, has exactly one material slot, no Nanite, no collision and no generated material or
texture left over in the folder. The conversion is the standard glTF one and nothing more:

    glTF is right-handed, Y-up, metres   ->   Unreal is left-handed, Z-up, centimetres
    UE.x = -glTF.z * 100      UE.y = glTF.x * 100      UE.z = glTF.y * 100

That map has determinant -1 (a handedness flip), so a triangle wound counter-clockwise (glTF's
front face) comes out clockwise in Unreal numbers - which is exactly the winding Geometry Script's
DynamicMesh calls front-facing (see the FLIP note in build_campus.py), so the index order is passed
through untouched. The model's own pivot is KEPT: the origin is the harness hook at the bottom of the
risers, which is the point the C++ hangs on the diver's shoulders.

Everything lands in /Game/CampusVehicles/Parachute, which the cooker already takes whole
(DirectoriesToAlwaysCook=/Game/CampusVehicles in Config/DefaultGame.ini).

Re-runnable: it deletes and rebuilds its own four assets.
"""
import json
import math
import os
import struct
import traceback

import unreal

SOURCE = os.environ.get("PARACHUTE_GLB", r"C:\Users\durga\Downloads\parachute_simple.glb")
ROOT = "/Game/CampusVehicles/Parachute"
MESH = "SM_Parachute"
TEXTURE = "T_Parachute_BaseColor"
MASTER = "M_Parachute_Canopy"
INSTANCE = "MI_Parachute_Canopy"

# The three numbers Campus/CampusBRPlane.cpp hangs the model with (CampusChuteModel::OverShoulders).
# Stated here too so the log can check them against the mesh that was actually built.
GAME_SCALE = 0.70
SHOULDER_RISE_DECOR = 85.0

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
SCRATCH = os.path.join(unreal.Paths.project_saved_dir(), "ParachuteSource")
REPORT = os.environ.get("PARACHUTE_REPORT", os.path.join(unreal.Paths.project_saved_dir(), "import_parachute_report.txt"))
out = []


def log(message):
    out.append(str(message))
    try:
        with open(REPORT, "w") as handle:
            handle.write("\n".join(out))
    except OSError:
        pass
    unreal.log(str(message))


# ------------------------------------------------------------------------------------------ the glb
GLB_MAGIC = 0x46546C67
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942
# accessor componentType -> (struct code, byte size, value that is 1.0 when the accessor is normalized)
COMPONENT = {
    5120: ("b", 1, 127.0), 5121: ("B", 1, 255.0), 5122: ("h", 2, 32767.0),
    5123: ("H", 2, 65535.0), 5125: ("I", 4, 1.0), 5126: ("f", 4, 1.0),
}
COMPONENTS_PER = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_glb(path):
    """(gltf json, binary chunk). Data only: nothing in the file is executed or imported by name."""
    with open(path, "rb") as handle:
        blob = handle.read()
    magic, version, length = struct.unpack_from("<III", blob, 0)
    if magic != GLB_MAGIC:
        raise RuntimeError("%s is not a binary glTF" % path)
    gltf, binary, offset = None, None, 12
    while offset < min(length, len(blob)):
        chunk_length, chunk_type = struct.unpack_from("<II", blob, offset)
        body = blob[offset + 8:offset + 8 + chunk_length]
        if chunk_type == CHUNK_JSON:
            gltf = json.loads(body.decode("utf-8"))
        elif chunk_type == CHUNK_BIN:
            binary = body
        offset += 8 + chunk_length
    if gltf is None:
        raise RuntimeError("no JSON chunk in %s" % path)
    log("glb v%d, %d bytes, generator %s" % (version, length, gltf.get("asset", {}).get("generator")))
    extras = gltf.get("asset", {}).get("extras", {})
    if extras:
        log("  asset says: %s by %s, %s" % (extras.get("title"), extras.get("author"), extras.get("license")))
    return gltf, binary


def accessor(gltf, binary, index):
    """One accessor as a list of tuples, honouring byteStride and normalized integers."""
    spec = gltf["accessors"][index]
    code, size, divisor = COMPONENT[spec["componentType"]]
    count_per = COMPONENTS_PER[spec["type"]]
    view = gltf["bufferViews"][spec["bufferView"]]
    start = view.get("byteOffset", 0) + spec.get("byteOffset", 0)
    stride = view.get("byteStride") or size * count_per
    scale = divisor if spec.get("normalized") else 1.0
    layout = "<%d%s" % (count_per, code)
    values = []
    for element in range(spec["count"]):
        raw = struct.unpack_from(layout, binary, start + element * stride)
        values.append(tuple(v / scale for v in raw) if scale != 1.0 else raw)
    return values


def identity():
    return [[1.0 if row == column else 0.0 for column in range(4)] for row in range(4)]


def from_gltf_matrix(flat):
    """glTF stores a matrix column-major; this returns rows[row][column]."""
    return [[flat[column * 4 + row] for column in range(4)] for row in range(4)]


def from_trs(node):
    translation = node.get("translation", [0.0, 0.0, 0.0])
    scale = node.get("scale", [1.0, 1.0, 1.0])
    x, y, z, w = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    rows = [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0.0],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0.0],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]
    for row in range(3):
        for column in range(3):
            rows[row][column] *= scale[column]
        rows[row][3] = translation[row]
    return rows


def matrix_multiply(left, right):
    return [[sum(left[row][k] * right[k][column] for k in range(4)) for column in range(4)] for row in range(4)]


def world_matrices(gltf):
    """Every node's composed world matrix, walked from the scene roots."""
    nodes = gltf.get("nodes", [])
    scene = gltf["scenes"][gltf.get("scene", 0)]
    world = {}

    def walk(index, parent):
        node = nodes[index]
        local = from_gltf_matrix(node["matrix"]) if "matrix" in node else from_trs(node)
        here = matrix_multiply(parent, local)
        world[index] = here
        for child in node.get("children", []):
            walk(child, here)

    for root in scene.get("nodes", []):
        walk(root, identity())
    return world


def apply(matrix, point, translate):
    bias = 1.0 if translate else 0.0
    return tuple(sum(matrix[row][column] * point[column] for column in range(3)) + matrix[row][3] * bias
                 for row in range(3))


def to_unreal(point):
    """glTF metres, Y-up, right-handed -> Unreal centimetres, Z-up, left-handed."""
    return (-point[2] * 100.0, point[0] * 100.0, point[1] * 100.0)


def to_unreal_direction(vector):
    turned = (-vector[2], vector[0], vector[1])
    length = math.sqrt(sum(v * v for v in turned))
    return tuple(v / length for v in turned) if length > 1e-6 else (0.0, 0.0, 1.0)


class Piece(object):
    """One glTF primitive, already in Unreal space."""

    def __init__(self):
        self.points, self.normals, self.uvs, self.triangles = [], [], [], []


def read_pieces(gltf, binary):
    world = world_matrices(gltf)
    pieces = []
    for index, node in enumerate(gltf.get("nodes", [])):
        if "mesh" not in node:
            continue
        matrix = world[index]
        for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
            if primitive.get("mode", 4) != 4:
                log("  SKIP primitive mode %s (not triangles)" % primitive.get("mode"))
                continue
            attributes = primitive["attributes"]
            piece = Piece()
            positions = accessor(gltf, binary, attributes["POSITION"])
            piece.points = [to_unreal(apply(matrix, p, True)) for p in positions]
            if "NORMAL" in attributes:
                piece.normals = [to_unreal_direction(apply(matrix, n, False))
                                 for n in accessor(gltf, binary, attributes["NORMAL"])]
            else:
                piece.normals = [(0.0, 0.0, 1.0)] * len(piece.points)
            if "TEXCOORD_0" in attributes:
                piece.uvs = [(u[0], u[1]) for u in accessor(gltf, binary, attributes["TEXCOORD_0"])]
            else:
                piece.uvs = [(0.0, 0.0)] * len(piece.points)
            if "indices" in primitive:
                flat = [v[0] for v in accessor(gltf, binary, primitive["indices"])]
            else:
                flat = list(range(len(piece.points)))
            # Index order kept as-is: see the handedness note in this file's docstring.
            piece.triangles = [(flat[t], flat[t + 1], flat[t + 2]) for t in range(0, len(flat) - 2, 3)]
            pieces.append(piece)
    return pieces


def extract_base_colour(gltf, binary):
    """Write the material's baseColorTexture out of the glb, for the texture import below."""
    materials = gltf.get("materials", [])
    if not materials:
        return None
    pbr = materials[0].get("pbrMetallicRoughness", {})
    reference = pbr.get("baseColorTexture")
    if not reference:
        log("  the glb has no baseColorTexture")
        return None
    texture = gltf["textures"][reference["index"]]
    image = gltf["images"][texture["source"]]
    if "bufferView" not in image:
        log("  baseColorTexture is not embedded (uri %s)" % image.get("uri"))
        return None
    view = gltf["bufferViews"][image["bufferView"]]
    start = view.get("byteOffset", 0)
    blob = binary[start:start + view["byteLength"]]
    os.makedirs(SCRATCH, exist_ok=True)
    extension = ".png" if "png" in image.get("mimeType", "") else ".jpg"
    path = os.path.join(SCRATCH, TEXTURE + extension)
    with open(path, "wb") as handle:
        handle.write(blob)
    log("  base colour %s (%d bytes, %s)" % (path, len(blob), image.get("mimeType")))
    return path


# ------------------------------------------------------------------------------------------ assets
def import_texture(source):
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = ROOT
    task.destination_name = TEXTURE
    task.automated = True
    task.replace_existing = True
    task.save = True
    AT.import_asset_tasks([task])
    texture = unreal.load_asset("%s/%s" % (ROOT, TEXTURE))
    if not texture:
        log("ERR texture import produced nothing")
        return None
    # A 512 x 256 colour map: sRGB, GPU-compressed (ASTC/ETC on Android), mips from the World group.
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    EAL.save_loaded_asset(texture)
    log("texture %-26s %d x %d  %s" % (TEXTURE, texture.blueprint_get_size_x(), texture.blueprint_get_size_y(),
                                       texture.get_editor_property("compression_settings")))
    return texture


def build_material(texture):
    """Unlit, opaque, TWO-SIDED, emissive = Texture * Color.

    Unlit like every other campus surface (build_campus.py's masters): identical on every phone, no
    shadow, no light to pay for, and this model's own texture already has its shading painted in.
    Two-sided because the glTF material says doubleSided and the diver spends the whole descent
    looking at the UNDERSIDE of the canopy - a backface-culled dome is a hole in the sky. It is 580
    triangles, so the doubled fragment cost is a rounding error next to being wrong.
    Its own master rather than one of build_campus.py's: that script owns the names in
    /Game/Campus/Materials and rebuilds them from its recipe, and no campus master is textured AND
    two-sided AND opaque.
    """
    path = "%s/%s" % (ROOT, MASTER)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    material = AT.create_asset(MASTER, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    sample = MEL.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -600, -100)
    sample.set_editor_property("parameter_name", "Texture")
    if texture:
        sample.set_editor_property("texture", texture)
    tint = MEL.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 180)
    tint.set_editor_property("parameter_name", "Color")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    shaded = MEL.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 0)
    MEL.connect_material_expressions(sample, "RGB", shaded, "A")
    MEL.connect_material_expressions(tint, "", shaded, "B")
    MEL.connect_material_property(shaded, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)

    instance_path = "%s/%s" % (ROOT, INSTANCE)
    if EAL.does_asset_exist(instance_path):
        EAL.delete_asset(instance_path)
    instance = AT.create_asset(INSTANCE, ROOT, unreal.MaterialInstanceConstant,
                               unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(instance, material)
    if texture:
        MEL.set_material_instance_texture_parameter_value(instance, "Texture", texture)
    MEL.set_material_instance_vector_parameter_value(instance, "Color", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    EAL.save_loaded_asset(instance)
    log("material %-25s unlit / opaque / two-sided, and %s" % (MASTER, INSTANCE))
    return instance


def build_mesh(pieces, material):
    dynamic = unreal.DynamicMesh()
    unreal.GeometryScript_Materials.enable_material_i_ds(dynamic)
    triangles = 0
    for piece in pieces:
        buffers = unreal.GeometryScriptSimpleMeshBuffers()
        buffers.vertices = [unreal.Vector(*point) for point in piece.points]
        buffers.normals = [unreal.Vector(*normal) for normal in piece.normals]
        buffers.uv0 = [unreal.Vector2D(*uv) for uv in piece.uvs]
        buffers.vertex_colors = [unreal.LinearColor(1.0, 1.0, 1.0, 1.0)] * len(piece.points)
        buffers.triangles = [unreal.IntVector(*tri) for tri in piece.triangles]
        # One material id for the whole model: the glb has a single material and a single draw call is
        # the point - fifty of these can be in the sky at once.
        unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic, buffers, 0)
        triangles += len(piece.triangles)
    path = "%s/%s" % (ROOT, MESH)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.enable_collision = False   # cosmetic: the diver's capsule is the only collider in the drop
    options.enable_nanite = False      # Android / ES3.1
    options.enable_recompute_normals = False   # the glb's own normals, converted
    options.enable_recompute_tangents = False  # unlit: no normal map, no tangent basis needed
    mesh, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(
        dynamic, path, options)
    if mesh is None:
        raise RuntimeError("could not create %s (%s)" % (path, outcome))
    slots = list(mesh.get_editor_property("static_materials"))
    while len(slots) < 1:
        slots.append(unreal.StaticMaterial())
    slots[0] = unreal.StaticMaterial(material_interface=material, material_slot_name="S0")
    mesh.set_editor_property("static_materials", slots)
    mesh.set_editor_property("light_map_resolution", 4)  # unlit: no lightmap is ever built
    EAL.save_loaded_asset(mesh)
    return mesh, triangles


# ------------------------------------------------------------------------------------------ report
def measure(pieces):
    """Bounds of the whole model and of the canopy alone, in Unreal centimetres."""
    everything = [point for piece in pieces for point in piece.points]
    low = [min(point[axis] for point in everything) for axis in range(3)]
    high = [max(point[axis] for point in everything) for axis in range(3)]
    return low, high


def main():
    log("source %s" % SOURCE)
    if not os.path.isfile(SOURCE):
        log("ERR no such file - set PARACHUTE_GLB")
        return
    gltf, binary = read_glb(SOURCE)
    pieces = read_pieces(gltf, binary)
    if not pieces:
        log("ERR the glb has no triangle geometry")
        return
    vertices = sum(len(piece.points) for piece in pieces)
    low, high = measure(pieces)
    log("model in Unreal space: %d primitives, %d verts" % (len(pieces), vertices))
    log("  x %8.1f .. %8.1f cm   (fore-aft, the canopy's chord)" % (low[0], high[0]))
    log("  y %8.1f .. %8.1f cm   (left-right, the canopy's span)" % (low[1], high[1]))
    log("  z %8.1f .. %8.1f cm   (up, 0 = the harness hook under the risers)" % (low[2], high[2]))
    log("  size %.1f x %.1f x %.1f cm, footprint centre (%.1f, %.1f)"
        % (high[0] - low[0], high[1] - low[1], high[2] - low[2],
           (low[0] + high[0]) * 0.5, (low[1] + high[1]) * 0.5))
    if high[1] - low[1] < high[0] - low[0]:
        log("  WARN the span is NOT the Y axis - the C++ relative rotation needs a yaw")

    texture = import_texture(extract_base_colour(gltf, binary))
    material = build_material(texture)
    mesh, triangles = build_mesh(pieces, material)
    box = mesh.get_bounding_box()
    log("mesh %-29s %d tris, %d slots" % (MESH, triangles, len(mesh.get_editor_property("static_materials"))))
    log("  asset bounds  x %.1f .. %.1f   y %.1f .. %.1f   z %.1f .. %.1f cm"
        % (box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z))
    log("  saved %s" % ("%s/%s" % (ROOT, MESH)))
    EAL.save_directory(ROOT, only_if_is_dirty=False, recursive=True)

    # What the C++ will see once CampusChuteModel::OverShoulders scales it (the numbers the diver
    # actually gets): shoulders are 55 cm over the capsule centre, which is 83 cm over the feet.
    scale = GAME_SCALE
    log("at the game scale %.2f (CampusChuteModel::OverShoulders):" % scale)
    log("  canopy span %.0f cm, chord %.0f cm, total height %.0f cm"
        % ((high[1] - low[1]) * scale, (high[0] - low[0]) * scale, (high[2] - low[2]) * scale))
    log("  centring offset (%.1f, %.1f, 0) cm" % (-(low[0] + high[0]) * 0.5 * scale, -(low[1] + high[1]) * 0.5 * scale))
    log("  diver: apex %.0f cm over the shoulders = %.0f cm over the feet"
        % (high[2] * scale, high[2] * scale + 55.0 + 83.0))
    log("  decor jumper: apex %.0f cm over its feet" % (high[2] * scale + SHOULDER_RISE_DECOR))


try:
    main()
except Exception:
    log("ERR " + traceback.format_exc())
log("DONE")
