"""
Low-poly gun art: one static mesh per weapon id in CampusWeapons.h (m4a1 ... deagle).

    UnrealEditor-Cmd.exe CollegeCampusGame.uproject -run=pythonscript -script=Scripts/build_guns.py
    python Scripts/build_guns.py --preview      (no editor: just re-renders Docs/guns_preview.png)

What it does:
  1. builds each gun out of boxes and octagonal cylinders with Geometry Script (the same
     SimpleMeshBuffers -> create_new_static_mesh_asset_from_mesh path as build_campus.py),
     well under 600 triangles each, and saves it to /Game/Weapons/Guns/SM_Gun_<id>
  2. tints it with 2-3 unlit material instances under build_campus.py's own flat master
     /Game/Campus/Materials/M_Campus_Unlit_Opaque, so the guns read like the rest of the game
  3. adds a "Muzzle" socket at the barrel tip and a "Grip" socket at the hand position
  4. renders Docs/guns_preview.png - a side/top orthographic projection of the very same boxes

Axis convention is SM_Rifle's, so a gun drops into the hand exactly like the rifle does
(Campus/CampusCharacter.cpp, namespace CampusGun): origin AT the grip, barrel along +Y, up +Z,
the AR muzzle at +Y 51 cm / +Z 6 cm. Guns that are not ARs have a different barrel tip on purpose
(the pistol is short, the LMG is long); Campus/CampusGunMeshes.h hands those offsets to C++.

Re-runnable: it deletes and rewrites its own assets under /Game/Weapons/Guns.
"""
import math
import os
import sys
import traceback

try:
    import unreal
except ImportError:
    unreal = None

ROOT = "/Game/Weapons/Guns"
MATERIALS = ROOT + "/Materials"
MASTER = "/Game/Campus/Materials/M_Campus_Unlit_Opaque"
RIFLE = "/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle"
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREVIEW = os.path.join(PROJECT, "Docs", "guns_preview.png")
REPORT = os.path.join(PROJECT, "Docs", "build_guns_report.txt")
MAX_TRIS = 600
# Geometry Script's DynamicMesh treats (b - a) x (c - a) as pointing INTO the surface (build_campus.py)
FLIP = True

out = []


def log(message):
    out.append(str(message))
    try:
        with open(REPORT, "w") as handle:
            handle.write("\n".join(out))
    except OSError:
        pass
    if unreal:
        unreal.log(str(message))
    else:
        print(message)


# ------------------------------------------------------------------------------------------- palette
DARK = "26282b"      # polymer / furniture black
METAL = "3f4348"     # receiver grey
STEEL = "5a5e66"     # bright metal: barrels, bipods, rails
WOOD = "7a4a22"      # AK furniture
TAN = "9a8257"        # SCAR flat dark earth
BONE = "b9b2a0"      # XM8 shell
CHROME = "8d9199"    # Deagle slide


def srgb_to_linear(channel):
    return channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4


def hex_linear(value):
    return tuple(srgb_to_linear(int(value[index:index + 2], 16) / 255.0) for index in (0, 2, 4))


# ------------------------------------------------------------------------------------------ geometry
# A part is a box or an octagonal cylinder in gun space (cm): centre, size, colour, optional pitch
# about +X (so a magazine or a pistol grip can rake back without a second primitive).
def box(colour, centre, size, pitch=0.0):
    return {"kind": "box", "c": colour, "o": centre, "s": size, "rx": pitch}


def cyl(colour, centre, radius, length, axis="y", sides=8):
    return {"kind": "cyl", "c": colour, "o": centre, "r": radius, "l": length, "axis": axis, "sides": sides}


BOX_FACES = (
    ((1, 0, 0), ((0.5, -0.5, -0.5), (0.5, 0.5, -0.5), (0.5, 0.5, 0.5), (0.5, -0.5, 0.5))),
    ((-1, 0, 0), ((-0.5, 0.5, -0.5), (-0.5, -0.5, -0.5), (-0.5, -0.5, 0.5), (-0.5, 0.5, 0.5))),
    ((0, 1, 0), ((0.5, 0.5, -0.5), (-0.5, 0.5, -0.5), (-0.5, 0.5, 0.5), (0.5, 0.5, 0.5))),
    ((0, -1, 0), ((-0.5, -0.5, -0.5), (0.5, -0.5, -0.5), (0.5, -0.5, 0.5), (-0.5, -0.5, 0.5))),
    ((0, 0, 1), ((0.5, -0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, 0.5), (-0.5, -0.5, 0.5))),
    ((0, 0, -1), ((-0.5, -0.5, -0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5), (0.5, -0.5, -0.5))),
)


def _pitch(point, radians):
    """rotate about +X: +Y tips toward +Z"""
    if not radians:
        return point
    cosine, sine = math.cos(radians), math.sin(radians)
    return (point[0], point[1] * cosine - point[2] * sine, point[1] * sine + point[2] * cosine)


def part_quads(part):
    """every part as a list of (normal, 4 world points) - the one geometry source for mesh AND preview"""
    quads = []
    if part["kind"] == "box":
        centre, size = part["o"], part["s"]
        radians = math.radians(part["rx"])
        for normal, corners in BOX_FACES:
            points = []
            for corner in corners:
                local = _pitch((corner[0] * size[0], corner[1] * size[1], corner[2] * size[2]), radians)
                points.append((centre[0] + local[0], centre[1] + local[1], centre[2] + local[2]))
            quads.append((_pitch(normal, radians), points))
        return quads
    # cylinder: a prism about its axis, flat-shaded, with a fan cap at each end
    axis = part["axis"]
    sides, radius, half = part["sides"], part["r"], part["l"] * 0.5
    centre = part["o"]
    index = {"x": 0, "y": 1, "z": 2}[axis]
    first, second = [k for k in (0, 1, 2) if k != index]

    def point(angle, end):
        value = [0.0, 0.0, 0.0]
        value[index] = end * half
        value[first] = math.cos(angle) * radius
        value[second] = math.sin(angle) * radius
        return (centre[0] + value[0], centre[1] + value[1], centre[2] + value[2])

    ring = [2.0 * math.pi * step / sides for step in range(sides)]
    for step in range(sides):
        angle, nxt = ring[step], ring[(step + 1) % sides]
        mid = (angle + nxt) * 0.5
        normal = [0.0, 0.0, 0.0]
        normal[first] = math.cos(mid)
        normal[second] = math.sin(mid)
        quads.append((tuple(normal), (point(angle, -1), point(nxt, -1), point(nxt, 1), point(angle, 1))))
    for end in (1, -1):
        normal = [0.0, 0.0, 0.0]
        normal[index] = float(end)
        order = ring if end > 0 else list(reversed(ring))
        # the cap as (sides - 2) triangles, written as quads where it divides evenly
        hub = point(order[0], end)
        for step in range(1, sides - 1, 2):
            second_point = point(order[step], end)
            third = point(order[step + 1], end)
            fourth = point(order[min(step + 2, sides - 1)], end)
            quads.append((tuple(normal), (hub, second_point, third, fourth)))
    return quads


class Section(object):
    """vertices / normals / uvs / colours / triangles for one material of one mesh"""

    def __init__(self):
        self.p, self.n, self.uv, self.c, self.t = [], [], [], [], []

    def tri(self, a, b, c):
        self.t.append((a, c, b) if FLIP else (a, b, c))

    def quad(self, normal, points):
        base = len(self.p)
        for point in points:
            self.p.append(point)
            self.n.append(normal)
            self.uv.append((0.0, 0.0))
            self.c.append((1.0, 1.0, 1.0))
        self.tri(base, base + 1, base + 2)
        if points[2] != points[3]:
            self.tri(base, base + 2, base + 3)


# ------------------------------------------------------------------------------------------ the guns
def receiver(body, furniture, muzzle_y, barrel_radius=0.95, body_len=30.0, body_z=5.0, body_w=4.4):
    """the shape every long gun shares: receiver, barrel to the tip, pistol grip, trigger guard"""
    front = body_len * 0.5 + 3.0
    return [
        box(body, (0.0, body_len * 0.5 - 6.0, body_z), (body_w, body_len, 7.0)),
        cyl(STEEL, (0.0, (front + muzzle_y) * 0.5 - 1.0, 6.0), barrel_radius, muzzle_y - front + 1.0),
        box(furniture, (0.0, -1.5, -4.5), (3.2, 4.6, 10.0), pitch=-16.0),
        box(body, (0.0, 3.4, -0.6), (2.4, 7.0, 1.2)),
    ]


GUNS = [
    # ---------------------------------------------------------------- assault rifles (rifle envelope)
    {"id": "m4a1", "name": "M4A1", "muzzle": 51.0, "colours": (METAL, DARK, STEEL), "parts": lambda: (
        receiver(METAL, DARK, 51.0) + [
            box(DARK, (0.0, 26.0, 6.0), (3.8, 16.0, 3.8)),                 # handguard
            box(METAL, (0.0, 6.0, 9.6), (2.6, 26.0, 1.8)),                  # flat-top rail
            box(METAL, (0.0, -3.0, 11.4), (1.6, 5.0, 2.6)),                 # rear sight
            box(DARK, (0.0, -16.0, 5.2), (4.0, 20.0, 6.4)),                 # carbine stock
            box(DARK, (0.0, -24.5, 5.2), (4.6, 3.0, 7.6)),                  # butt plate
            box(DARK, (0.0, 7.0, -6.6), (2.8, 6.4, 11.0), pitch=-3.0),      # STANAG mag
            cyl(DARK, (0.0, 49.0, 6.0), 1.5, 5.0),                          # flash hider
        ])},
    {"id": "ak47", "name": "AK-47", "muzzle": 51.0, "colours": (METAL, WOOD, STEEL), "parts": lambda: (
        receiver(METAL, WOOD, 51.0, barrel_radius=1.05, body_len=28.0) + [
            box(WOOD, (0.0, 24.0, 6.4), (3.6, 13.0, 4.2)),                  # wooden handguard
            box(WOOD, (0.0, 34.0, 6.6), (3.0, 6.0, 5.2)),                   # gas block / upper wood
            box(WOOD, (0.0, -17.0, 3.2), (3.4, 22.0, 6.0), pitch=4.0),      # wooden stock
            box(METAL, (0.0, 4.2, -5.6), (2.6, 6.2, 9.0), pitch=-10.0),     # curved mag, upper half
            box(METAL, (0.0, 1.4, -13.0), (2.6, 6.6, 8.0), pitch=-34.0),    # curved mag, lower half
            cyl(METAL, (0.0, 41.0, 8.4), 1.1, 7.0),                         # gas tube
            box(METAL, (0.0, 48.5, 6.0), (2.2, 4.0, 3.0)),                  # front sight block
        ])},
    {"id": "m416", "name": "M416", "muzzle": 51.0, "colours": (DARK, STEEL, METAL), "parts": lambda: (
        receiver(DARK, DARK, 51.0, body_len=30.0) + [
            box(STEEL, (0.0, 28.0, 6.0), (3.4, 20.0, 3.4)),                 # free-float rail tube
            box(STEEL, (0.0, 28.0, 8.2), (2.2, 20.0, 1.2)),                 # top rail
            box(STEEL, (0.0, 28.0, 3.8), (2.2, 20.0, 1.2)),                 # bottom rail
            box(DARK, (0.0, 24.0, -1.0), (2.2, 3.0, 7.0)),                   # vertical foregrip
            box(DARK, (0.0, 6.0, 9.6), (2.6, 26.0, 1.6)),                    # receiver rail
            box(DARK, (0.0, -15.0, 5.2), (4.2, 19.0, 6.6)),                  # adjustable stock
            box(DARK, (0.0, 7.0, -6.6), (2.8, 6.4, 11.0), pitch=-3.0),       # mag
        ])},
    {"id": "scar", "name": "SCAR", "muzzle": 51.0, "colours": (TAN, DARK, STEEL), "parts": lambda: (
        receiver(TAN, DARK, 51.0, body_len=32.0, body_w=5.0) + [
            box(TAN, (0.0, 28.0, 6.0), (5.0, 22.0, 6.4)),                   # blocky slab handguard
            box(DARK, (0.0, 28.0, 9.6), (3.0, 22.0, 1.4)),                   # rail
            box(TAN, (0.0, -14.0, 5.6), (5.0, 18.0, 7.6)),                   # blocky folding stock
            box(DARK, (0.0, -23.5, 5.6), (5.2, 3.0, 9.0)),                   # butt pad
            box(DARK, (0.0, 7.0, -6.4), (3.0, 6.6, 11.0)),                   # straight mag
            box(DARK, (0.0, -1.0, 10.0), (2.0, 6.0, 2.4)),                    # rear sight
        ])},
    {"id": "an94", "name": "AN-94", "muzzle": 57.0, "colours": (METAL, DARK, STEEL), "colour_note": "long barrel",
     "parts": lambda: (
        receiver(METAL, DARK, 57.0, barrel_radius=0.85, body_len=28.0) + [
            box(DARK, (0.0, 22.0, 6.2), (3.2, 12.0, 4.0)),                  # short handguard
            cyl(STEEL, (0.0, 44.0, 6.0), 1.25, 16.0),                        # long exposed barrel shroud
            cyl(STEEL, (0.0, 55.0, 6.0), 1.7, 6.0),                          # muzzle brake
            box(DARK, (0.0, -15.0, 4.6), (3.2, 20.0, 6.0)),                  # side-folding stock
            box(METAL, (0.0, 4.6, -6.0), (2.6, 6.2, 10.0), pitch=-12.0),     # raked mag
            box(METAL, (0.0, 0.0, 9.8), (2.2, 10.0, 2.0)),                    # dust cover rail
        ])},
    {"id": "xm8", "name": "XM8", "muzzle": 51.0, "colours": (BONE, DARK, STEEL), "parts": lambda: (
        [
            box(BONE, (0.0, 6.0, 5.0), (4.6, 40.0, 8.0)),                    # one-piece bullpup shell
            cyl(BONE, (0.0, 32.0, 6.0), 2.3, 14.0),                          # rounded forward shroud
            cyl(STEEL, (0.0, 45.0, 6.0), 0.9, 13.0),                         # barrel to the tip
            box(BONE, (0.0, -16.0, 4.6), (4.4, 16.0, 7.2)),                  # integral butt
            cyl(BONE, (0.0, -23.0, 4.6), 3.4, 4.0),                          # rounded butt cap
            box(BONE, (0.0, 2.0, 10.4), (3.0, 26.0, 3.0)),                   # raised carry rail
            box(DARK, (0.0, -1.5, -4.5), (3.2, 4.6, 10.0), pitch=-16.0),     # grip
            box(BONE, (0.0, 3.4, -0.6), (2.6, 7.0, 1.4)),                     # trigger guard
            box(DARK, (0.0, 9.0, -5.4), (2.8, 6.0, 8.6)),                     # mag
        ])},
    # --------------------------------------------------------------------------------- SMG / LMG / pistol
    {"id": "mp40", "name": "MP40", "muzzle": 40.0, "colours": (DARK, STEEL, WOOD), "parts": lambda: (
        [
            cyl(DARK, (0.0, 8.0, 5.4), 2.2, 24.0),                           # tube receiver
            cyl(STEEL, (0.0, 31.0, 5.4), 1.0, 20.0),                         # thin barrel to the tip
            cyl(DARK, (0.0, 22.0, 5.4), 1.8, 5.0),                           # barrel nut
            box(DARK, (0.0, -2.0, 0.6), (3.0, 10.0, 4.0)),                    # lower housing
            box(WOOD, (0.0, -1.5, -4.2), (3.0, 4.2, 9.0), pitch=-10.0),      # grip
            box(DARK, (0.0, -12.0, 4.4), (2.0, 14.0, 2.0)),                   # folding strut
            box(DARK, (0.0, -20.0, 4.4), (4.0, 2.4, 6.0)),                    # folding shoulder rest
            box(STEEL, (2.6, 4.0, -3.0), (2.4, 5.0, 12.0), pitch=-4.0),      # side-mounted stick mag
            box(DARK, (0.0, 2.0, 8.2), (1.8, 4.0, 1.8)),                      # rear sight
        ])},
    {"id": "m249", "name": "M249", "muzzle": 60.0, "colours": (METAL, DARK, STEEL), "parts": lambda: (
        receiver(METAL, DARK, 60.0, barrel_radius=1.3, body_len=34.0, body_w=5.6) + [
            box(METAL, (0.0, 14.0, 12.0), (3.0, 22.0, 2.4)),                 # carry handle bar
            box(METAL, (0.0, 3.5, 13.6), (2.4, 5.0, 3.2)),                    # handle riser
            box(DARK, (0.0, 11.0, -7.0), (7.0, 16.0, 12.0)),                  # 100-round box mag
            box(METAL, (0.0, -14.0, 5.2), (4.4, 18.0, 7.0)),                  # buttstock
            box(METAL, (0.0, -23.0, 5.2), (5.0, 3.0, 9.0)),                   # butt plate
            cyl(STEEL, (0.0, 40.0, 7.8), 1.0, 14.0),                          # gas tube over the barrel
            box(STEEL, (1.2, 44.0, -2.0), (1.4, 1.4, 14.0), pitch=14.0),      # bipod leg right
            box(STEEL, (-1.2, 44.0, -2.0), (1.4, 1.4, 14.0), pitch=14.0),     # bipod leg left
            box(STEEL, (0.0, 45.0, 3.4), (3.6, 2.4, 2.4)),                    # bipod yoke
            cyl(STEEL, (0.0, 57.5, 6.0), 1.9, 5.0),                           # flash hider
        ])},
    {"id": "deagle", "name": "Desert Eagle", "muzzle": 26.0, "colours": (CHROME, DARK, STEEL), "parts": lambda: (
        [
            box(CHROME, (0.0, 8.0, 5.6), (3.0, 22.0, 6.0)),                  # slab slide
            box(CHROME, (0.0, 8.0, 9.2), (2.0, 22.0, 1.4)),                   # slide rib
            cyl(STEEL, (0.0, 21.5, 5.6), 1.1, 10.0),                          # barrel
            box(CHROME, (0.0, 1.0, 1.4), (2.6, 8.0, 3.2)),                    # frame
            box(DARK, (0.0, -2.2, -4.6), (3.0, 5.0, 11.0), pitch=-18.0),     # grip with the mag in it
            box(CHROME, (0.0, 3.0, -0.8), (2.2, 6.0, 1.2)),                   # trigger guard
            box(CHROME, (0.0, -1.0, 9.6), (2.4, 2.4, 1.6)),                   # rear sight
            box(CHROME, (0.0, 17.0, 9.0), (1.6, 1.6, 1.6)),                   # front sight
        ])},
]

# Where the hand holds it: the grip is the mesh origin for every gun (SM_Rifle's own convention),
# so the Grip socket sits on the origin and the Muzzle socket on the barrel tip.
GRIP_SOCKET = (0.0, 0.0, 0.0)
MUZZLE_Z = 6.0


# ------------------------------------------------------------------------------------------- assets
def _editor():
    return unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools(), unreal.MaterialEditingLibrary


_instances = {}


def material(colour):
    if colour in _instances:
        return _instances[colour]
    eal, tools, mel = _editor()
    master = unreal.load_asset(MASTER)
    if not master:
        log("MISSING master %s - run Scripts/build_campus.py first" % MASTER)
        return None
    name = "MI_Gun_%s" % colour
    path = "%s/%s" % (MATERIALS, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    instance = tools.create_asset(name, MATERIALS, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(instance, master)
    red, green, blue = hex_linear(colour)
    mel.set_material_instance_vector_parameter_value(instance, "Color", unreal.LinearColor(red, green, blue, 1.0))
    eal.save_loaded_asset(instance)
    _instances[colour] = instance
    log("  material MI_Gun_%s" % colour)
    return instance


def add_socket(mesh, name, location):
    """StaticMeshSocket via whichever route this engine build exposes"""
    try:
        # owned by the mesh, so the socket is serialised with it rather than left transient
        socket = unreal.new_object(unreal.StaticMeshSocket, outer=mesh, name=name)
    except Exception:
        socket = unreal.StaticMeshSocket()
    socket.set_editor_property("socket_name", name)
    socket.set_editor_property("relative_location", unreal.Vector(*location))
    socket.set_editor_property("relative_rotation", unreal.Rotator(0.0, 0.0, 0.0))
    try:
        mesh.add_socket(socket)
        return True
    except Exception:
        pass
    try:
        sockets = [s for s in list(mesh.get_editor_property("sockets") or []) if s.get_editor_property("socket_name") != name]
        sockets.append(socket)
        mesh.set_editor_property("sockets", sockets)
        return True
    except Exception as error:
        log("  WARN socket %s not set (%s)" % (name, error))
        return False


def write_mesh(gun, parts):
    eal, _tools, _mel = _editor()
    sections, order = {}, []
    for part in parts:
        colour = part["c"]
        if colour not in sections:
            sections[colour] = Section()
            order.append(colour)
        section = sections[colour]
        for normal, points in part_quads(part):
            section.quad(normal, points)
    mesh_path = "%s/SM_Gun_%s" % (ROOT, gun["id"])
    dynamic = unreal.DynamicMesh()
    unreal.GeometryScript_Materials.enable_material_i_ds(dynamic)
    slots, triangles = [], 0
    for colour in order:
        section = sections[colour]
        buffers = unreal.GeometryScriptSimpleMeshBuffers()
        buffers.vertices = [unreal.Vector(*point) for point in section.p]
        buffers.normals = [unreal.Vector(*normal) for normal in section.n]
        buffers.uv0 = [unreal.Vector2D(*uv) for uv in section.uv]
        buffers.vertex_colors = [unreal.LinearColor(c[0], c[1], c[2], 1.0) for c in section.c]
        buffers.triangles = [unreal.IntVector(*tri) for tri in section.t]
        unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic, buffers, len(slots))
        slots.append(material(colour))
        triangles += len(section.t)
    if eal.does_asset_exist(mesh_path):
        eal.delete_asset(mesh_path)
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.enable_collision = False
    options.enable_nanite = False
    options.enable_recompute_normals = False
    options.enable_recompute_tangents = False
    mesh, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dynamic, mesh_path, options)
    if mesh is None:
        raise RuntimeError("could not create %s (%s)" % (mesh_path, outcome))
    materials = list(mesh.get_editor_property("static_materials"))
    while len(materials) < len(slots):
        materials.append(unreal.StaticMaterial())
    for index, slot in enumerate(slots):
        materials[index] = unreal.StaticMaterial(material_interface=slot, material_slot_name="S%d" % index)
    mesh.set_editor_property("static_materials", materials)
    mesh.set_editor_property("light_map_resolution", 4)   # unlit: no lightmap is ever built
    add_socket(mesh, "Muzzle", (0.0, gun["muzzle"], MUZZLE_Z))
    add_socket(mesh, "Grip", GRIP_SOCKET)
    eal.save_loaded_asset(mesh)
    box_min, box_max = bounds(parts)
    log("SM_Gun_%-7s %3d tris  %d slots  length %.0f cm (Y %.0f..%.0f)  muzzle +Y %.0f%s"
        % (gun["id"], triangles, len(slots), box_max[1] - box_min[1], box_min[1], box_max[1], gun["muzzle"],
           "  OVER BUDGET" if triangles > MAX_TRIS else ""))
    return triangles


def bounds(parts):
    low = [1e9, 1e9, 1e9]
    high = [-1e9, -1e9, -1e9]
    for part in parts:
        for _normal, points in part_quads(part):
            for point in points:
                for axis in (0, 1, 2):
                    low[axis] = min(low[axis], point[axis])
                    high[axis] = max(high[axis], point[axis])
    return low, high


def report_rifle():
    """the reference the guns copy: SM_Rifle's own size, pivot and sockets"""
    mesh = unreal.load_asset(RIFLE)
    if not mesh:
        log("WARN %s did not load - gun axes still follow namespace CampusGun" % RIFLE)
        return
    box = mesh.get_bounding_box()
    log("SM_Rifle  X %.1f..%.1f  Y %.1f..%.1f  Z %.1f..%.1f  (%.0f x %.0f x %.0f cm)"
        % (box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z,
           box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z))
    try:
        names = [str(s.get_editor_property("socket_name")) for s in list(mesh.get_editor_property("sockets") or [])]
        log("SM_Rifle sockets: %s" % (", ".join(names) if names else "none"))
    except Exception:
        pass


# ------------------------------------------------------------------------------------------ preview
def render_preview():
    """side (X-ignored) and top (Z-ignored) orthographic fills of the same boxes, one row per gun"""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        log("preview skipped: PIL is not available in this interpreter "
            "(run `python Scripts/build_guns.py --preview` with the system python)")
        return None
    scale, pad, label = 5.0, 14, 86
    cell_w = int(100.0 * scale) + pad * 2   # -32 .. +68 cm: the LMG's muzzle and bipod still fit
    side_h = int(34.0 * scale)
    top_h = int(16.0 * scale)
    row_h = side_h + top_h + pad * 3
    # pale sheet: half the guns are near-black polymer and would vanish on a dark background
    image = Image.new("RGB", (label + cell_w, row_h * len(GUNS) + pad), (232, 232, 234))
    draw = ImageDraw.Draw(image)
    try:
        font = ImageFont.truetype("arialbd.ttf", 16)
    except Exception:
        font = ImageFont.load_default()

    def shade(colour, normal, axis):
        red, green, blue = (int(colour[i:i + 2], 16) for i in (0, 2, 4))
        facing = abs(normal[2]) if axis == "side" else abs(normal[1])
        lift = 0.68 + 0.32 * facing
        return (min(255, int(red * lift)), min(255, int(green * lift)), min(255, int(blue * lift)))

    for row, gun in enumerate(GUNS):
        parts = gun["parts"]()
        top = pad + row * row_h
        draw.text((8, top + 6), gun["id"], font=font, fill=(26, 28, 32))
        draw.text((8, top + 26), "%.0fcm" % gun["muzzle"], font=font, fill=(118, 122, 130))
        origin_x = label + pad + int(32.0 * scale)
        for axis, base, height in (("side", top + pad, side_h), ("top", top + pad * 2 + side_h, top_h)):
            middle = base + height * 0.5
            draw.line((label, middle, label + cell_w, middle), fill=(206, 206, 210))
            quads = []
            for part in parts:
                for normal, points in part_quads(part):
                    depth = (sum(p[0] for p in points) / 4.0) if axis == "side" else (sum(p[2] for p in points) / 4.0)
                    quads.append((depth, part["c"], normal, points))
            quads.sort(key=lambda entry: entry[0])
            for _depth, colour, normal, points in quads:
                flat = [(origin_x + point[1] * scale,
                         middle - (point[2] if axis == "side" else point[0]) * scale) for point in points]
                draw.polygon(flat, fill=shade(colour, normal, axis))
            tip = (origin_x + gun["muzzle"] * scale, middle - (MUZZLE_Z if axis == "side" else 0.0) * scale)
            draw.ellipse((tip[0] - 3, tip[1] - 3, tip[0] + 3, tip[1] + 3), outline=(255, 90, 70), width=2)
            draw.ellipse((origin_x - 3, middle - 3, origin_x + 3, middle + 3), outline=(90, 200, 255), width=2)
    os.makedirs(os.path.dirname(PREVIEW), exist_ok=True)
    image.save(PREVIEW)
    log("preview %s (%d x %d)  red ring = Muzzle, blue ring = Grip" % (PREVIEW, image.width, image.height))
    return PREVIEW


# --------------------------------------------------------------------------------------------- main
def main():
    if unreal is None:
        render_preview()
        return
    report_rifle()
    total = 0
    for gun in GUNS:
        total += write_mesh(gun, gun["parts"]())
    unreal.EditorAssetLibrary.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    log("%d guns, %d triangles total, in %s" % (len(GUNS), total, ROOT))
    render_preview()


try:
    main()
except Exception:
    log("ERR " + traceback.format_exc())
log("DONE")
if unreal is None and "--preview" not in sys.argv:
    print("note: `unreal` is not importable here; only the preview was produced")
