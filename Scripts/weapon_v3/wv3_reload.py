"""Explicit detachable-part membership; never detach a magwell or a magazine tube.

Split AFTER baking to preserve both UV sets and the shared skin material.
Add reviewed entries here when supporting another weapon's reload.
"""
import os, json, re
import bpy, bmesh
from mathutils import Vector

MAGAZINE_PARTS = {
    'mp40': {'Magazine', 'Mag rib', 'Mag floorplate'},
    'ak47': {'Magazine', 'Mag rib', 'Mag floorplate', 'Mag lug'},
    'deagle': {'Magazine', 'Magazine base'},
    'aa12': {'Drum magazine', 'Drum cap'},
    'bizon': {'Helical magazine', 'Mag ridge'},
    'p90': {'Top magazine'},
    'scar': {'Magazine', 'Mag floorplate', 'Mag window'},
    'm4a1': {'Magazine', 'Mag rib', 'Mag floorplate'},
    'groza': {'Magazine', 'Mag rib', 'Mag floorplate'},
    'svd': {'Magazine', 'Mag rib'},
    **{k: {'Magazine', 'Mag floorplate'} for k in ('awm', 'mp5', 'ump', 'thompson', 'vector', 'famas', 'mag7')},
    **{k: {'Magazine'} for k in ('an94', 'aug', 'xm8', 'vss', 'm14', 'woodpecker', 'm82', 'm416')},
    **{k: {'Magazine', 'Magazine base'} for k in ('usp', 'g18', 'mac10')},
    'm249': {'Ammo pouch'}, 'gatling': {'Ammo box', 'Ammo belt'},
    'm500': {'Cylinder'}, 'm1917': {'Cylinder'}, 'mgl': {'Cylinder', 'Chamber'},
    'kord': {'Belt round'}, 'plasma': {'Energy cell'}, 'railgun': {'Capacitors'},
    'm79': {'Barrel', 'Bore', 'Forend', 'Leaf sight', 'Front sight'},
}
RELOAD_ROLES = {'m249': 'AmmoPouch', 'gatling': 'AmmoBox', 'm500': 'Cylinder',
    'm1917': 'Cylinder', 'mgl': 'Cylinder', 'kord': 'AmmoBelt', 'plasma': 'EnergyCell',
    'railgun': 'EnergyCell', 'm79': 'BarrelAssembly'}
RELOAD_TYPES = {**{k: 'tube_individual_rounds' for k in ('m1014','m1887','spas12','m870')},
    'kar98': 'internal_magazine', 'sks': 'internal_magazine', 'm60': 'belt_feed',
    'crossbow': 'single_bolt', 'handcannon': 'single_round', 'charge_buster': 'energy_charge',
    'healing_pistol': 'energy_recharge', **{k: 'not_applicable_melee' for k in ('bat','katana','knife','machete','pan','scythe')}}

def tag_parts(gid, parts):
    names = MAGAZINE_PARTS.get(gid)
    if not names:
        return
    found = set()
    for ob, _, _ in parts:
        name = re.sub(r'\.\d+$', '', ob.name)
        selected = name in names
        if selected:
            found.add(name)
        a = ob.data.attributes.new('reload_magazine', 'INT', 'POINT')
        a.data.foreach_set('value', [int(selected)] * len(ob.data.vertices))
        ob['animation_part'] = 'magazine' if selected else 'body'
    missing = names - found
    if missing:
        raise RuntimeError(f'{gid}: missing magazine parts {sorted(missing)}')

def export_reload(g, gid, out):
    if gid not in MAGAZINE_PARTS:
        return {'status': 'no_detachable_magazine', 'reload_type': RELOAD_TYPES.get(gid, 'requires_review')}
    folder = os.path.join(out, 'Reload', gid)
    os.makedirs(folder, exist_ok=True)
    a = g.data.attributes.get('reload_magazine')
    assert a, 'Reload membership lost during mesh join/bake'
    chosen = [v.co.copy() for v in g.data.vertices if a.data[v.index].value]
    assert chosen and len(chosen) < len(g.data.vertices)
    pivot = Vector(((min(v.x for v in chosen) + max(v.x for v in chosen)) / 2,
                    (min(v.y for v in chosen) + max(v.y for v in chosen)) / 2,
                    max(v.z for v in chosen)))
    collection = bpy.data.collections.new(gid + '_reload_exports')
    bpy.context.scene.collection.children.link(collection)
    counts = {}
    moving_role = RELOAD_ROLES.get(gid, 'Magazine')
    for role, flag in [('Body', 0), (moving_role, 1)]:
        ob = bpy.data.objects.new(f'SM_{gid}_{role}', g.data.copy())
        collection.objects.link(ob)
        bm = bmesh.new(); bm.from_mesh(ob.data)
        layer = bm.verts.layers.int.get('reload_magazine')
        assert layer
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if v[layer] != flag], context='VERTS')
        bm.to_mesh(ob.data); bm.free()
        assert len(ob.data.uv_layers) == 2
        counts[role] = len(ob.data.polygons)
        if flag:
            for v in ob.data.vertices:
                v.co -= pivot
        bpy.ops.object.select_all(action='DESELECT')
        ob.select_set(True); bpy.context.view_layer.objects.active = ob
        bpy.ops.export_scene.fbx(filepath=os.path.join(folder, ob.name + '.fbx'),
            use_selection=True, object_types={'MESH'}, axis_forward='Y', axis_up='Z',
            apply_unit_scale=True, add_leaf_bones=False, bake_anim=False,
            mesh_smooth_type='OFF', use_tspace=True, path_mode='STRIP')
        bpy.ops.export_scene.gltf(filepath=os.path.join(folder, ob.name + '.glb'),
            use_selection=True, export_format='GLB', export_tangents=True)
        # In the saved Blender scene, display the magazine seated in its original position.
        if flag:
            ob.location = pivot
        ob['reload_role'] = role
    assert sum(counts.values()) == len(g.data.polygons), 'Split lost faces'
    collection.hide_render = True; collection.hide_viewport = True
    bpy.ops.object.select_all(action='DESELECT')
    g.select_set(True); bpy.context.view_layer.objects.active = g
    record = {'status': 'exported', 'body': f'SM_{gid}_Body',
        'magazine': f'SM_{gid}_{moving_role}', 'moving_role': moving_role,
        'socket': moving_role + 'Seat',
        'seat_translation_cm_blender': list(pivot), 'seat_rotation_degrees': [0, 0, 0],
        'axis': '+Y muzzle, +Z up', 'triangles': counts,
        'material': f'Same baked material and skin parameters as SM_{gid}',
        'validation': 'Face counts conserved; both UV layers retained',
        'animation': 'Separate static meshes; no skeletal animation clips authored'}
    with open(os.path.join(folder, 'reload.json'), 'w') as f:
        json.dump(record, f, indent=2)
    return record
