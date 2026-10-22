"""Run in Blender after preview_refined.py to verify deliverable split and packed images."""
import bpy, os, json, math
from mathutils.kdtree import KDTree
root = os.environ['WEAPON_V3_OUT']
manifest = json.load(open(os.path.join(root, 'manifest.json')))
report = []
for rec in manifest['weapons']:
    gid = rec['id']
    bpy.ops.wm.open_mainfile(filepath=os.path.join(root, 'Blend', gid + '.blend'))
    gun = bpy.data.objects['SM_' + gid]
    # Hidden collections are not evaluated: enable them before reading world matrices.
    bpy.data.collections[gid + '_reload_exports'].hide_viewport = False
    bpy.context.view_layer.update()
    parts = [bpy.data.objects[f'SM_{gid}_{role}'] for role in ('Body', 'Magazine')]
    tree = KDTree(len(gun.data.vertices))
    for v in gun.data.vertices:
        tree.insert(v.co, v.index)
    tree.balance()
    max_error = 0
    for part in parts:
        assert len(part.data.uv_layers) == 2
        assert len(part.data.materials) == 1
        for v in part.data.vertices:
            _, index, distance = tree.find(part.matrix_world @ v.co)
            max_error = max(max_error, distance)
        for layer in part.data.uv_layers:
            assert all(math.isfinite(c) for uv in layer.data for c in uv.uv)
    assert max_error < .0001, (gid, max_error)
    assert sum(len(p.data.polygons) for p in parts) == len(gun.data.polygons)
    missing = [im.name for im in bpy.data.images if im.source == 'FILE' and not im.packed_file
               and not os.path.isfile(bpy.path.abspath(im.filepath))]
    assert not missing, missing
    report.append({'id': gid, 'triangles': len(gun.data.polygons),
        'reassembled_max_error_cm': max_error, 'two_uv_layers': True,
        'single_material_per_piece': True, 'face_count_conserved': True,
        'missing_images': missing, 'unreal_runtime_tested': False})
json.dump(report, open(os.path.join(root, 'RefinementValidation.json'), 'w'), indent=2)
print('REFINEMENT_VALIDATION_PASSED', report, flush=True)
