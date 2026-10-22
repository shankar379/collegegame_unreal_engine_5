"""Create portable normal/royal Blender variants and comparison previews.
Run after build_weapon_library_v3.py; WEAPON_V3_OUT selects the build folder.
"""
import bpy, os, sys, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wv3_surface import make_skins, game_material, render_views, _save
root = os.environ['WEAPON_V3_OUT']
manifest = json.load(open(os.path.join(root, 'manifest.json')))
ids = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
for rec in manifest['weapons']:
    gid = rec['id']
    if ids and gid not in ids:
        continue
    bpy.ops.wm.open_mainfile(filepath=os.path.join(root, 'Blend', gid + '.blend'))
    g = bpy.data.objects['SM_' + gid]
    files = {k: bpy.data.images.load(os.path.join(root, p), check_existing=True)
             for k, p in rec['textures'].items() if k != 'SkinTemplate'}
    skins = make_skins(root)
    royal = skins['RoyalGold']
    if gid == 'mp40':
        # Coordinates in the exact UV1 template; gold stock/mag, red central panel.
        s = rec['uv1_side_projection']
        vv, uu = np.mgrid[0:1024, 0:1024].astype(np.float32) / 1023
        y = s['y_min'] + uu * s['size']
        z = s['z_centre'] + (vv - .5) * s['size']
        rgba = np.ones((1024, 1024, 4), np.float32)
        rgba[..., :3] = (1.0, .62, .17)
        panel = (y > 0) & (y < 22.8) & (z > 2.8) & (z < 8.4)
        barrel = (y > 39) & (y < 51)
        rgba[panel | barrel, :3] = (.38, .009, .025)
        # Fine angular inlay, with restrained spacing instead of noisy all-over stripes.
        inlay = panel & (np.abs(((y + .7 * z) % 7) - 3.5) < .13)
        rim = panel & ((z < 3.1) | (z > 8.1) | (y < .3) | (y > 22.5))
        engraving = (~panel) & (np.abs(np.sin(y * 1.6 + np.sin(z * 1.3)) * np.cos(z * 1.6)) < .012)
        rgba[engraving, :3] = (.80, .43, .10)
        rgba[inlay | rim, :3] = (1., .82, .37)
        path = os.path.join(root, 'Textures', 'Skins', 'T_Skin_mp40_Royal.png')
        royal = (_save('MP40_Royal', rgba, path, False), .9, .25)
    normal = g.data.materials[0]
    if gid == 'deagle':
        render_views(g, os.path.join(root, 'Previews', gid + '_quick.png'), samples=10,
            views=[('right', (1, 0, 0), True), ('front34', (.75, .85, .35), False)])
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(root, 'Blend', gid + '.blend'))
    material = game_material(gid, files, royal)
    g.data.materials[0] = material
    for ob in bpy.data.collections[gid + '_reload_exports'].objects:
        ob.data.materials.clear(); ob.data.materials.append(material)
    render_views(g, os.path.join(root, 'Previews', gid + '_royal.png'), samples=16,
        views=[('side', (1, 0, 0), True), ('front34', (.75, .85, .35), False)])
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(root, 'Blend', gid + '_Royal.blend'))
print('REFINED_PREVIEWS_COMPLETE', flush=True)
