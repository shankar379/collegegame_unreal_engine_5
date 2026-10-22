"""Campus Combat - Weapon Library V3 (realistic pass).

Run:  blender --background --factory-startup --python build_weapon_library_v3.py -- ak47 [m4a1 ...] [--size 2048] [--no-render]
No gun names = build every gun in wv3_guns.GUNS.

Writes ArtSource/WeaponLibraryV3/:
  FBX/SM_<id>.fbx              game mesh, one material slot, UV0 (texture atlas) + UV1 (side projection for skins)
  GLB/<id>.glb                 same mesh with baked textures embedded (three.js / web)
  Textures/T_<id>_*.png        BaseColor, ORM, Normal (DirectX), Normal_GL, SkinMask, SkinTemplate
  Textures/Skins/T_Skin_*.png  example skins that work on every gun
  UV/<id>_UV0.svg              texture layout for painters
  Previews/<id>_views.png      six directions;  <id>_skins.png  the three example skins
  Blend/<id>.blend             editable source: separate named parts + the final mesh
  manifest.json
"""
import bpy, sys, os, json, time, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib
import glob
import wv3_kit, wv3_surface, wv3_guns
for m in (wv3_kit, wv3_surface, wv3_guns):
    importlib.reload(m)
# every wv3_guns_*.py file registers more guns into wv3_guns.GUNS
for f in sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wv3_guns_*.py'))):
    name = os.path.splitext(os.path.basename(f))[0]
    if name in sys.modules:
        importlib.reload(sys.modules[name])
    else:
        importlib.import_module(name)
from wv3_kit import PARTS, reset, clear_cutters
from wv3_surface import bake_gun, render_views, make_skins, game_material, KINDS
from wv3_reload import tag_parts, export_reload

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.environ.get('WEAPON_V3_OUT', os.path.join(ROOT, 'ArtSource', 'WeaponLibraryV3'))
argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
SIZE = 2048
if '--size' in argv:
    SIZE = int(argv[argv.index('--size') + 1])
    del argv[argv.index('--size'):argv.index('--size') + 2]
RENDER = '--no-render' not in argv
QUICK = '--quick' in argv
if QUICK:
    SIZE = min(SIZE, 1024)
argv = [a for a in argv if not a.startswith('--')]
IDS = argv or list(wv3_guns.GUNS)
for d in ('FBX', 'GLB', 'Textures', 'UV', 'Previews', 'Blend'):
    os.makedirs(os.path.join(OUT, d), exist_ok=True)
T0 = time.time()


def log(*a):
    print('[WV3 %5.0fs]' % (time.time() - T0), *a, flush=True)


def clean_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.curves, bpy.data.cameras, bpy.data.lights):
        for b in list(coll):
            if b.users == 0:
                coll.remove(b)


def placeholder_material(kind):
    name = 'KIND_' + kind
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
        m['kind'] = kind
        m.diffuse_color = (*KINDS[kind]['a'], 1)
    return m


def build(gid):
    log('build', gid)
    clean_scene()
    reset()
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    sc.unit_settings.scale_length = .01
    meta = wv3_guns.GUNS[gid]()
    clear_cutters()
    tag_parts(gid, PARTS)
    # skin flag + material kind on every part
    for o, kind, skin in PARTS:
        a = o.data.attributes.new('skin', 'FLOAT', 'POINT')
        a.data.foreach_set('value', [1.0 if skin else 0.0] * len(o.data.vertices))
        o.data.materials.clear()
        o.data.materials.append(placeholder_material(kind))
    # editable source: keep the named parts in a collection
    src = bpy.data.collections.new(gid + '_parts')
    sc.collection.children.link(src)
    for o, kind, skin in PARTS:
        o['kind'] = kind
        o['skin'] = skin
        for c in list(o.users_collection):
            c.objects.unlink(o)
        src.objects.link(o)
    copies = []
    for o, kind, skin in PARTS:
        c = o.copy()
        c.data = o.data.copy()
        sc.collection.objects.link(c)
        copies.append(c)
    bpy.ops.object.select_all(action='DESELECT')
    for c in copies:
        c.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    g = bpy.context.object
    g.name = 'SM_' + gid
    g.data.name = 'SM_' + gid
    src.hide_render = True
    src.hide_viewport = True
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(g.data)
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
    bm.to_mesh(g.data)
    bm.free()
    tris = len(g.data.polygons)
    log('  parts', len(PARTS), 'triangles', tris)

    # UV0: unique texture atlas
    me = g.data
    while me.uv_layers:
        me.uv_layers.remove(me.uv_layers[0])
    me.uv_layers.new(name='UV0')
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(52), island_margin=.006, area_weight=.0, correct_aspect=True, scale_to_bounds=False)
    try:
        bpy.ops.uv.pack_islands(rotate=True, margin=.004, shape_method='CONCAVE')
    except Exception:
        bpy.ops.uv.pack_islands(rotate=True, margin=.004)
    bpy.ops.object.mode_set(mode='OBJECT')
    # UV1: side projection, square, so one skin painted on the side template fits every gun
    vs = me.vertices
    ymin = min(v.co.y for v in vs); ymax = max(v.co.y for v in vs)
    zmin = min(v.co.z for v in vs); zmax = max(v.co.z for v in vs)
    S = max(ymax - ymin, zmax - zmin)
    zc = (zmin + zmax) / 2
    uv1 = me.uv_layers.new(name='UV1')
    for loop in me.loops:
        co = vs[loop.vertex_index].co
        uv1.data[loop.index].uv = ((co.y - ymin) / S, (co.z - zc) / S + .5)
    me.uv_layers.active = me.uv_layers['UV0']
    me.uv_layers['UV0'].active_render = True
    svg(g, gid)

    log('  baking textures', SIZE)
    files = bake_gun(g, gid, OUT, SIZE, log)

    sockets = {'Grip': [0, 0, 0], 'Muzzle': [0, meta['muzzle'][0], meta['muzzle'][1]],
               'Optic': [0, meta['optic'][0], meta['optic'][1]], 'Magazine': [0, meta['mag'][0], meta['mag'][1]],
               'FX_Core': [0, 8, meta['bore_z'] + 3]}
    for name, p in sockets.items():
        e = bpy.data.objects.new('SOCKET_' + name, None)
        e.empty_display_size = 2
        e.location = p
        sc.collection.objects.link(e)
        e.parent = g

    # exports
    bpy.ops.object.select_all(action='DESELECT')
    g.select_set(True)
    bpy.context.view_layer.objects.active = g
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, 'FBX', 'SM_%s.fbx' % gid), use_selection=True, object_types={'MESH'},
                             axis_forward='Y', axis_up='Z', apply_unit_scale=True, add_leaf_bones=False, bake_anim=False,
                             mesh_smooth_type='OFF', use_tspace=True, path_mode='STRIP')
    try:
        bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, 'GLB', gid + '.glb'), use_selection=True, export_format='GLB',
                                  export_tangents=True, export_texcoords=True, export_normals=True, export_yup=True)
    except Exception as e:
        log('  glb export failed:', e)

    # skin template: flat-lit side view in exact UV1 space
    reload_data = export_reload(g, gid, OUT)
    template(g, gid, ymin, zc, S)
    rec = {'id': gid, 'triangles': tris, 'parts': len(PARTS), 'bounds_cm': [[round(min(v.co[k] for v in vs), 2), round(max(v.co[k] for v in vs), 2)] for k in range(3)],
           'sockets_cm': sockets, 'texture_size': SIZE, 'uv1_side_projection': {'y_min': ymin, 'z_centre': zc, 'size': S},
           'textures': {k: 'Textures/T_%s_%s.png' % (gid, k) for k in ('BaseColor', 'ORM', 'Normal', 'Normal_GL', 'SkinMask', 'SkinTemplate')}}
    rec['reload'] = reload_data
    if RENDER:
        log('  previews')
        if QUICK:
            render_views(g, os.path.join(OUT, 'Previews', gid + '_quick.png'), samples=10,
                         views=[('right', (1, 0, 0), True), ('front34', (.75, .85, .35), False), ('rear34', (-.8, -.75, .45), False)], log=log)
            bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, 'Blend', gid + '.blend'))
            return rec
        render_views(g, os.path.join(OUT, 'Previews', gid + '_views.png'), log=log)
        skins = make_skins(OUT)
        base_mat = g.data.materials[0]
        shots = []
        for name, sk in skins.items():
            sm = game_material(gid, files, sk)
            g.data.materials[0] = sm
            p = os.path.join(OUT, 'Previews', '%s_skin_%s.png' % (gid, name))
            render_views(g, p, views=[('front34', (.75, .85, .35), False)], log=log)
            shots.append(p)
            g.data.materials[0] = base_mat
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, 'Blend', gid + '.blend'))
    log('  done', gid)
    return rec


def svg(o, gid):
    uv = o.data.uv_layers['UV0'].data
    polys = []
    for p in o.data.polygons:
        pts = ' '.join('%.1f,%.1f' % (uv[i].uv.x * 2048, (1 - uv[i].uv.y) * 2048) for i in p.loop_indices)
        polys.append('<polygon points="%s"/>' % pts)
    with open(os.path.join(OUT, 'UV', gid + '_UV0.svg'), 'w') as f:
        f.write('<svg xmlns="http://www.w3.org/2000/svg" width="2048" height="2048" viewBox="0 0 2048 2048"><rect width="2048" height="2048" fill="#1d232b"/>'
                '<g fill="#2e3a46" stroke="#9fc9e6" stroke-width=".6">' + ''.join(polys) + '</g></svg>')


def template(o, gid, ymin, zc, S):
    """Side view that lines up pixel-for-pixel with UV1: paint a skin over it, save, done."""
    sc = bpy.context.scene
    cam = bpy.data.objects.new('CAM_template', bpy.data.cameras.new('CAM_template'))
    sc.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = S
    cam.location = (200, ymin + S / 2, zc)
    cam.rotation_euler = (math.pi / 2, 0, math.pi / 2)
    sc.camera = cam
    sc.render.engine = 'CYCLES'
    sc.cycles.samples = 8
    sc.render.resolution_x = sc.render.resolution_y = 1024
    sc.render.film_transparent = True
    sc.view_settings.view_transform = 'Standard'
    # emission-only copy of the base colour so the template has no lighting baked in
    m = bpy.data.materials.new('TPL')
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    uvn = nt.nodes.new('ShaderNodeUVMap'); uvn.uv_map = 'UV0'
    t = nt.nodes.new('ShaderNodeTexImage'); t.image = bpy.data.images['T_%s_BaseColor' % gid]
    nt.links.new(uvn.outputs['UV'], t.inputs['Vector'])
    em = nt.nodes.new('ShaderNodeEmission'); nt.links.new(t.outputs['Color'], em.inputs['Color'])
    out = nt.nodes.new('ShaderNodeOutputMaterial'); nt.links.new(em.outputs[0], out.inputs['Surface'])
    keep = o.data.materials[0]
    o.data.materials[0] = m
    sc.render.filepath = os.path.join(OUT, 'Textures', 'T_%s_SkinTemplate.png' % gid)
    bpy.ops.render.render(write_still=True)
    o.data.materials[0] = keep
    sc.render.film_transparent = False
    bpy.data.materials.remove(m)
    bpy.data.objects.remove(cam)


manifest_path = os.path.join(OUT, 'manifest.json')
manifest = json.load(open(manifest_path)) if os.path.exists(manifest_path) else {'weapons': []}
import traceback
failed = []
for gid in IDS:
    try:
        rec = build(gid)
    except Exception:
        log('FAILED', gid)
        traceback.print_exc()
        failed.append(gid)
        continue
    # re-read first: another Blender process may have added guns since this one started
    manifest = json.load(open(manifest_path)) if os.path.exists(manifest_path) else {'weapons': []}
    manifest['weapons'] = [w for w in manifest['weapons'] if w['id'] != rec['id']] + [rec]
    manifest.update({'library': 'WeaponLibraryV3', 'axis': '+Y muzzle, +Z up, centimetres, grip at origin',
                     'material': 'one slot: BaseColor + ORM(AO,Rough,Metal) + Normal; skins via SkinMask.R on UV0 or UV1'})
    json.dump(manifest, open(manifest_path, 'w'), indent=2)
log('WV3_COMPLETE', IDS, 'failed:', failed)
