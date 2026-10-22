"""Weapon Library V3 - modelling kit.

Units: 1 Blender unit = 1 cm. Axes: +Y = muzzle direction, +Z = up, +X = the gun's right side.
The grip sits at the origin, so every gun lines up with the character's hand socket.

Every part is a real closed solid with rounded (bevelled) edges and smooth/hard normals,
the way a game weapon artist would build a mid-poly model. Each part carries:
  - a material "kind" (steel, wood, polymer ...), which drives the baked textures
  - a skin flag: True = a skin may repaint this part (receiver, stock, magazine ...),
    False = mechanical parts that stay metal (barrel, bolt, sights, screws ...)
"""
import bpy, bmesh, math
from mathutils import Vector, Matrix

PARTS = []          # parts of the gun being built: (object, kind, skin)
CUTTERS = []


def reset():
    PARTS.clear()
    CUTTERS.clear()


# ---------------------------------------------------------------- 2D path helpers
def path(*cmds, n=10):
    """SVG-like 2D path. Commands: ('M',a,b) ('L',a,b) ('Q',ca,cb,a,b) ('C',c1a,c1b,c2a,c2b,a,b).
    Returns a list of (a,b) points; a closing duplicate is removed."""
    pts = []
    cur = None
    for c in cmds:
        k = c[0]
        if k in ('M', 'L'):
            cur = (c[1], c[2])
            pts.append(cur)
        elif k == 'Q':
            p0, p1, p2 = cur, (c[1], c[2]), (c[3], c[4])
            for i in range(1, n + 1):
                t = i / n
                pts.append(tuple((1 - t) ** 2 * p0[j] + 2 * (1 - t) * t * p1[j] + t * t * p2[j] for j in (0, 1)))
            cur = p2
        elif k == 'C':
            p0, p1, p2, p3 = cur, (c[1], c[2]), (c[3], c[4]), (c[5], c[6])
            for i in range(1, n + 1):
                t = i / n
                pts.append(tuple((1 - t) ** 3 * p0[j] + 3 * (1 - t) ** 2 * t * p1[j] + 3 * (1 - t) * t * t * p2[j] + t ** 3 * p3[j] for j in (0, 1)))
            cur = p3
    out = []
    for p in pts:
        if not out or (abs(p[0] - out[-1][0]) > 1e-4 or abs(p[1] - out[-1][1]) > 1e-4):
            out.append(p)
    if len(out) > 2 and abs(out[0][0] - out[-1][0]) < 1e-4 and abs(out[0][1] - out[-1][1]) < 1e-4:
        out.pop()
    return out


def rrect(a0, b0, a1, b1, r, n=4):
    """Rounded rectangle as a point list."""
    r = min(r, (a1 - a0) / 2 - 1e-3, (b1 - b0) / 2 - 1e-3)
    pts = []
    for (ca, cb, start) in ((a1 - r, b0 + r, -90), (a1 - r, b1 - r, 0), (a0 + r, b1 - r, 90), (a0 + r, b0 + r, 180)):
        for i in range(n + 1):
            ang = math.radians(start + 90 * i / n)
            pts.append((ca + r * math.cos(ang), cb + r * math.sin(ang)))
    return pts


def band(line, thickness):
    """Turn an open polyline into a closed strip polygon of the given thickness (centred)."""
    def normals(pts):
        ns = []
        for i in range(len(pts)):
            a = pts[max(i - 1, 0)]
            b = pts[min(i + 1, len(pts) - 1)]
            d = Vector((b[0] - a[0], b[1] - a[1]))
            d.normalize()
            ns.append(Vector((-d.y, d.x)))
        return ns
    ns = normals(line)
    h = thickness / 2
    left = [(p[0] + n.x * h, p[1] + n.y * h) for p, n in zip(line, ns)]
    right = [(p[0] - n.x * h, p[1] - n.y * h) for p, n in zip(line, ns)]
    return left + right[::-1]


def arc(ca, cb, r, a0, a1, n=12):
    return [(ca + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cb + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


# ---------------------------------------------------------------- object helpers
def _obj(name, bm):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def _apply_mods(o):
    deps = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(o.evaluated_get(deps), preserve_all_data_layers=True, depsgraph=deps)
    old = o.data
    o.modifiers.clear()
    o.data = me
    if old.users == 0:
        bpy.data.meshes.remove(old)


def _map(plane, a, b, c):
    """Place 2D profile coordinates (a,b) plus depth c into 3D."""
    if plane == 'YZ':
        return (c, a, b)
    if plane == 'XZ':
        return (a, c, b)
    return (a, b, c)  # XY


def extrude(name, pts, depth, kind, plane='YZ', at=0.0, crown=0.0, inset=0.0, taper=None,
            bevel=0.1, skin=True, cuts=(), seg=2, sharp=32):
    """Extrude a 2D outline into a solid.
    plane 'YZ' = side outline extruded across X (receivers, stocks, grips, magazines),
    plane 'XZ' = front cross-section extruded along Y (dust covers, rails),
    plane 'XY' = top outline extruded along Z.
    crown/inset pillow the two big faces so stocks and grips read rounded, not flat.
    taper(a,b) -> width scale, e.g. grips that are thinner at the bottom."""
    bm = bmesh.new()
    h = depth / 2
    A = [bm.verts.new(_map(plane, a, b, at - h)) for a, b in pts]
    B = [bm.verts.new(_map(plane, a, b, at + h)) for a, b in pts]
    fa = bm.faces.new(A)
    fb = bm.faces.new(B)
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((A[i], A[j], B[j], B[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    if crown or inset:
        bmesh.ops.inset_individual(bm, faces=[fa, fb], thickness=inset, depth=crown, use_even_offset=True)
    if taper:
        ax = {'YZ': 0, 'XZ': 1, 'XY': 2}[plane]
        for v in bm.verts:
            co = list(v.co)
            a, b = {'YZ': (co[1], co[2]), 'XZ': (co[0], co[2]), 'XY': (co[0], co[1])}[plane]
            co[ax] = at + (co[ax] - at) * taper(a, b)
            v.co = co
    o = _obj(name, bm)
    return finish(o, kind, skin, bevel, cuts, seg, sharp)


def box(name, center, size, kind, bevel=0.08, skin=False, cuts=(), rot=None):
    x, y, z = center
    sx, sy, sz = size
    pts = [(y - sy / 2, z - sz / 2), (y + sy / 2, z - sz / 2), (y + sy / 2, z + sz / 2), (y - sy / 2, z + sz / 2)]
    o = extrude(name, pts, sx, kind, 'YZ', at=x, bevel=0, skin=skin)
    if rot:
        o.data.transform(Matrix.Translation(Vector(center)) @ Matrix.Rotation(math.radians(rot[1]), 4, rot[0]) @ Matrix.Translation(-Vector(center)))
    for c in cuts:
        _cut(o, c)
    if bevel:
        _bevel(o, bevel, 2, 32)
    return o


def lathe(name, prof, kind, at=(0, 0, 0), axis='Y', seg=24, closed=False, bevel=0.04, skin=False, cuts=(), sharp=40):
    """Revolve a (distance-along-axis, radius) profile: barrels, muzzle devices, scope tubes, pins.
    closed=True joins the last ring back to the first (hollow parts such as a muzzle with a bore)."""
    bm = bmesh.new()
    rings = []
    x0, y0, z0 = at
    for d, r in prof:
        ring = []
        for k in range(seg):
            ang = 2 * math.pi * k / seg
            c, s = r * math.cos(ang), r * math.sin(ang)
            if axis == 'Y':
                p = (x0 + c, y0 + d, z0 + s)
            elif axis == 'X':
                p = (x0 + d, y0 + c, z0 + s)
            else:
                p = (x0 + c, y0 + s, z0 + d)
            ring.append(bm.verts.new(p))
        rings.append(ring)
    pairs = list(zip(rings, rings[1:]))
    if closed:
        pairs.append((rings[-1], rings[0]))
    for r1, r2 in pairs:
        for k in range(seg):
            m = (k + 1) % seg
            bm.faces.new((r1[k], r1[m], r2[m], r2[k]))
    if not closed:
        bm.faces.new(rings[0])
        bm.faces.new(rings[-1][::-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = _obj(name, bm)
    return finish(o, kind, skin, bevel, cuts, 2, sharp)


def tube(name, pts3d, r, kind, skin=False, smooth=True, res=3, cyclic=False):
    """A round bar swept along a 3D path: trigger guards, wire stocks, springs, sling loops."""
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = r
    cu.bevel_resolution = res
    cu.use_fill_caps = True
    cu.resolution_u = 6
    if smooth and len(pts3d) > 2:
        sp = cu.splines.new('NURBS')
        sp.points.add(len(pts3d) - 1)
        for p, q in zip(sp.points, pts3d):
            p.co = (*q, 1)
        sp.use_endpoint_u = True
        sp.order_u = 3
    else:
        sp = cu.splines.new('POLY')
        sp.points.add(len(pts3d) - 1)
        for p, q in zip(sp.points, pts3d):
            p.co = (*q, 1)
    sp.use_cyclic_u = cyclic
    if cyclic:
        sp.use_endpoint_u = False
    tmp = bpy.data.objects.new(name + '_curve', cu)
    bpy.context.scene.collection.objects.link(tmp)
    deps = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(deps), depsgraph=deps)
    bpy.data.objects.remove(tmp)
    bpy.data.curves.remove(cu)
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return finish(o, kind, skin, 0, (), 2, 60)


# ---------------------------------------------------------------- cutters (booleans)
def cbox(center, size, rot=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    o = bpy.context.object
    o.scale = size
    if rot:
        o.rotation_euler[{'X': 0, 'Y': 1, 'Z': 2}[rot[0]]] = math.radians(rot[1])
    CUTTERS.append(o)
    return o


def ccyl(center, r, length, axis='X', seg=16):
    bpy.ops.mesh.primitive_cylinder_add(vertices=seg, radius=r, depth=length, location=center)
    o = bpy.context.object
    if axis == 'X':
        o.rotation_euler.y = math.pi / 2
    elif axis == 'Y':
        o.rotation_euler.x = math.pi / 2
    CUTTERS.append(o)
    return o


def cprism(pts, depth, plane='YZ', at=0.0):
    bm = bmesh.new()
    h = depth / 2
    A = [bm.verts.new(_map(plane, a, b, at - h)) for a, b in pts]
    B = [bm.verts.new(_map(plane, a, b, at + h)) for a, b in pts]
    bm.faces.new(A)
    bm.faces.new(B)
    for i in range(len(pts)):
        j = (i + 1) % len(pts)
        bm.faces.new((A[i], A[j], B[j], B[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = _obj('cutter', bm)
    CUTTERS.append(o)
    return o


def _cut(o, cutter):
    m = o.modifiers.new('cut', 'BOOLEAN')
    m.object = cutter
    m.operation = 'DIFFERENCE'
    m.solver = 'EXACT'
    _apply_mods(o)


def _bevel(o, width, seg, sharp):
    me = o.data
    bm = bmesh.new()
    bm.from_mesh(me)
    for f in bm.faces:
        f.smooth = True
    for e in bm.edges:
        e.smooth = not (len(e.link_faces) == 2 and e.calc_face_angle(0) > math.radians(sharp))
    bm.to_mesh(me)
    bm.free()
    if width:
        dims = o.dimensions
        w = min(width, max(min(d for d in dims if d > 1e-4) * 0.2, 0.01))
        b = o.modifiers.new('bevel', 'BEVEL')
        b.width = w
        b.segments = seg
        b.limit_method = 'ANGLE'
        b.angle_limit = math.radians(sharp)
        b.miter_outer = 'MITER_ARC'
        b.harden_normals = False
    wn = o.modifiers.new('wn', 'WEIGHTED_NORMAL')
    wn.keep_sharp = True
    wn.weight = 50
    _apply_mods(o)


def finish(o, kind, skin, bevel, cuts=(), seg=2, sharp=32):
    for c in cuts:
        _cut(o, c)
    _bevel(o, bevel, seg, sharp)
    PARTS.append((o, kind, skin))
    return o


def mirror_x(o):
    """Copy a part to the other side of the gun (x -> -x)."""
    for p, kind, skin in PARTS:
        if p is o:
            break
    me = o.data.copy()
    me.transform(Matrix.Scale(-1, 4, (1, 0, 0)))
    me.flip_normals()
    c = bpy.data.objects.new(o.name + '_L', me)
    bpy.context.scene.collection.objects.link(c)
    PARTS.append((c, kind, skin))
    return c


def clear_cutters():
    for c in CUTTERS:
        if c.name in bpy.data.objects:
            bpy.data.objects.remove(c)
    CUTTERS.clear()
