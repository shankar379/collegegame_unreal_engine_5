"""Unreal importer for Weapon Library V3 (run inside the editor or as a Python commandlet).

  UnrealEditor-Cmd.exe CollegeCampusGame.uproject -run=pythonscript -script="Scripts/weapon_v3/import_weapon_library_v3.py"
  Optional env: WEAPON_V3_FILTER=ak47,m4a1   WEAPON_V3_REIMPORT=1   WEAPON_V3_MAX_TEX=1024
  The phone build imports only the 9 store guns at 1024 px (everything under /Game/Weapons/LibraryV3 is cooked):
    WEAPON_V3_FILTER=m4a1,ak47,m416,scar,an94,xm8,mp40,m249,deagle  WEAPON_V3_MAX_TEX=1024

Creates under /Game/Weapons/LibraryV3:
  Meshes/SM_<id>            static mesh (FBX), 3 LODs, sockets Grip / Muzzle / Optic / Magazine / FX_Core
  Textures/<id>/T_<id>_*    BaseColor (sRGB), ORM (linear), Normal (normal map), SkinMask (linear), Emissive (sRGB, if any)
  Materials/M_WeaponV3      master material (below)
  Materials/MI_<id>         per-gun instance with that gun's textures
  Materials/Skins/T_Skin_*  example skins (NeonHex, GoldRelic, ArcticCircuit, RoyalGold)

Master material parameters (all on the instance, so skins are just parameter changes):
  BaseColorTex, ORMTex, NormalTex, SkinMaskTex, EmissiveTex   - the gun's own baked textures (UV0)
  SkinTexture (white = no skin), SkinStrength 0..1, PatternMode 0 = UV0 atlas / 1 = UV1 side projection,
  SkinTint, SkinMetallic, SkinRoughness, EmissiveIntensity
Skin logic = what the Blender previews show: skin * AO darkening, worn edges show bare metal,
painted only where SkinMask.R = 1 (receivers, stocks, magazines - never barrels, bolts, sights).
At runtime: CreateDynamicMaterialInstance on slot 0, set SkinTexture + SkinStrength=1 (+ PatternMode).
"""
import unreal as u
import os, json

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get('WEAPON_V3_OUT', os.path.join(os.path.dirname(os.path.dirname(HERE)), 'ArtSource', 'WeaponLibraryV3'))
ROOT = '/Game/Weapons/LibraryV3'
lib = u.EditorAssetLibrary
at = u.AssetToolsHelpers.get_asset_tools()
mel = u.MaterialEditingLibrary
manifest = json.load(open(os.path.join(SRC, 'manifest.json')))
FILTER = [x for x in os.environ.get('WEAPON_V3_FILTER', '').split(',') if x]
REIMPORT = os.environ.get('WEAPON_V3_REIMPORT', '') == '1'
MAX_TEX = int(os.environ.get('WEAPON_V3_MAX_TEX', '2048'))


def import_file(path, dest, name):
    t = u.AssetImportTask()
    t.filename = path
    t.destination_path = dest
    t.destination_name = name
    t.automated = True
    t.save = True
    t.replace_existing = True
    at.import_asset_tasks([t])
    return lib.load_asset(dest + '/' + name)


def texture(path, dest, name, kind):
    a = lib.load_asset(dest + '/' + name) if lib.does_asset_exist(dest + '/' + name) and not REIMPORT else import_file(path, dest, name)
    if kind == 'linear':
        a.set_editor_property('srgb', False)
        a.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_MASKS)
    elif kind == 'normal':
        a.set_editor_property('srgb', False)
        a.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP)
    a.set_editor_property('max_texture_size', MAX_TEX)
    lib.save_loaded_asset(a)
    return a


def node(m, cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, ao, b, bi):
    if not mel.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError('connect failed %s -> %s' % (ao, bi))


def master_material(defaults):
    path = ROOT + '/Materials/M_WeaponV3'
    if lib.does_asset_exist(path) and not REIMPORT:
        return lib.load_asset(path)
    m = at.create_asset('M_WeaponV3', ROOT + '/Materials', u.Material, u.MaterialFactoryNew())
    uv0 = node(m, u.MaterialExpressionTextureCoordinate, -1800, 0, coordinate_index=0)
    uv1 = node(m, u.MaterialExpressionTextureCoordinate, -1800, 200, coordinate_index=1)

    def tparam(name, x, y, tex, sampler, uv):
        n = node(m, u.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=tex, sampler_type=sampler)
        link(uv, '', n, 'UVs')
        return n
    S = u.MaterialSamplerType
    bc = tparam('BaseColorTex', -1400, -400, defaults['white'], S.SAMPLERTYPE_COLOR, uv0)
    orm = tparam('ORMTex', -1400, -100, defaults['orm'], S.SAMPLERTYPE_MASKS, uv0)
    nrm = tparam('NormalTex', -1400, 200, defaults['normal'], S.SAMPLERTYPE_NORMAL, uv0)
    msk = tparam('SkinMaskTex', -1400, 500, defaults['black'], S.SAMPLERTYPE_MASKS, uv0)
    emi = tparam('EmissiveTex', -1400, 800, defaults['black_srgb'], S.SAMPLERTYPE_COLOR, uv0)
    mode = node(m, u.MaterialExpressionScalarParameter, -1800, 400, parameter_name='PatternMode', default_value=1.0)
    skin_uv = node(m, u.MaterialExpressionLinearInterpolate, -1600, 300)
    link(uv0, '', skin_uv, 'A'); link(uv1, '', skin_uv, 'B'); link(mode, '', skin_uv, 'Alpha')
    sk = tparam('SkinTexture', -1400, 1100, defaults['white'], S.SAMPLERTYPE_COLOR, skin_uv)
    tint = node(m, u.MaterialExpressionVectorParameter, -1400, 1400, parameter_name='SkinTint', default_value=u.LinearColor(1, 1, 1, 1))
    strength = node(m, u.MaterialExpressionScalarParameter, -1400, 1550, parameter_name='SkinStrength', default_value=0.0)
    skin_metal = node(m, u.MaterialExpressionScalarParameter, -1400, 1650, parameter_name='SkinMetallic', default_value=0.4)
    skin_rough = node(m, u.MaterialExpressionScalarParameter, -1400, 1750, parameter_name='SkinRoughness', default_value=0.35)
    glow = node(m, u.MaterialExpressionScalarParameter, -1400, 1850, parameter_name='EmissiveIntensity', default_value=6.0)
    # skin colour: tint * texture, darkened by cavities (SkinMask.B), worn edges (SkinMask.G) show bare metal
    tinted = node(m, u.MaterialExpressionMultiply, -1100, 1100); link(sk, 'RGB', tinted, 'A'); link(tint, '', tinted, 'B')
    cav = node(m, u.MaterialExpressionOneMinus, -1100, 600); link(msk, 'B', cav, '')
    cav2 = node(m, u.MaterialExpressionLinearInterpolate, -950, 600, const_a=0.3, const_b=1.0); link(cav, '', cav2, 'Alpha')
    shaded = node(m, u.MaterialExpressionMultiply, -800, 1000); link(tinted, '', shaded, 'A'); link(cav2, '', shaded, 'B')
    bare = node(m, u.MaterialExpressionConstant3Vector, -800, 1250, constant=u.LinearColor(.55, .55, .56, 1))
    worn = node(m, u.MaterialExpressionLinearInterpolate, -650, 1000); link(shaded, '', worn, 'A'); link(bare, '', worn, 'B'); link(msk, 'G', worn, 'Alpha')
    amount = node(m, u.MaterialExpressionMultiply, -650, 700); link(msk, 'R', amount, 'A'); link(strength, '', amount, 'B')
    col = node(m, u.MaterialExpressionLinearInterpolate, -400, -300); link(bc, 'RGB', col, 'A'); link(worn, '', col, 'B'); link(amount, '', col, 'Alpha')
    rough = node(m, u.MaterialExpressionLinearInterpolate, -400, -50); link(orm, 'G', rough, 'A'); link(skin_rough, '', rough, 'B'); link(amount, '', rough, 'Alpha')
    metal = node(m, u.MaterialExpressionLinearInterpolate, -400, 150); link(orm, 'B', metal, 'A'); link(skin_metal, '', metal, 'B'); link(amount, '', metal, 'Alpha')
    em = node(m, u.MaterialExpressionMultiply, -400, 800); link(emi, 'RGB', em, 'A'); link(glow, '', em, 'B')
    P = u.MaterialProperty
    mel.connect_material_property(col, '', P.MP_BASE_COLOR)
    mel.connect_material_property(rough, '', P.MP_ROUGHNESS)
    mel.connect_material_property(metal, '', P.MP_METALLIC)
    mel.connect_material_property(orm, 'R', P.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(nrm, 'RGB', P.MP_NORMAL)
    mel.connect_material_property(em, '', P.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    lib.save_loaded_asset(m)
    return m


def solid_texture(name, rgb, srgb):
    """Tiny default textures generated from the V3 Textures folder if present, else imported from PNG we write."""
    path = os.path.join(SRC, 'Textures', 'Defaults', name + '.png')
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        import zlib, struct
        raw = b''.join(b'\x00' + bytes(rgb) * 4 for _ in range(4))
        def chunk(t, d):
            return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
        open(path, 'wb').write(png)
    return texture(path, ROOT + '/Textures/Defaults', name, 'srgb' if srgb else ('normal' if 'Normal' in name else 'linear'))


defaults = {'white': solid_texture('T_White', (255, 255, 255), True), 'black': solid_texture('T_Black', (0, 0, 0), False),
            'black_srgb': solid_texture('T_BlackColor', (0, 0, 0), True), 'orm': solid_texture('T_DefaultORM', (255, 140, 0), False),
            'normal': solid_texture('T_FlatNormal', (128, 128, 255), False)}
master = master_material(defaults)

# example skins
skin_dir = os.path.join(SRC, 'Textures', 'Skins')
if os.path.isdir(skin_dir):
    for f in sorted(os.listdir(skin_dir)):
        if f.endswith('.png'):  # all four: every gun sells all four styles (backend migration 032)
            texture(os.path.join(skin_dir, f), ROOT + '/Materials/Skins', f[:-4], 'srgb')

report = []
for rec in manifest['weapons']:
    gid = rec['id']
    if FILTER and gid not in FILTER:
        continue
    fbx = os.path.join(SRC, 'FBX', 'SM_%s.fbx' % gid)
    mesh_path = ROOT + '/Meshes/SM_' + gid
    if lib.does_asset_exist(mesh_path) and not REIMPORT:
        u.log('skip existing ' + gid)
        continue
    # textures
    tdir = ROOT + '/Textures/' + gid
    tex = {}
    for k, kind in (('BaseColor', 'srgb'), ('ORM', 'linear'), ('Normal', 'normal'), ('SkinMask', 'linear'), ('Emissive', 'srgb')):
        p = os.path.join(SRC, 'Textures', 'T_%s_%s.png' % (gid, k))
        if os.path.exists(p):
            tex[k] = texture(p, tdir, 'T_%s_%s' % (gid, k), kind)
    # mesh
    t = u.AssetImportTask()
    t.filename = fbx
    t.destination_path = ROOT + '/Meshes'
    t.destination_name = 'SM_' + gid
    t.automated = True
    t.save = True
    t.replace_existing = True
    opt = u.FbxImportUI()
    opt.import_mesh = True
    opt.import_materials = False
    opt.import_textures = False
    opt.import_as_skeletal = False
    opt.static_mesh_import_data.combine_meshes = True
    opt.static_mesh_import_data.generate_lightmap_u_vs = False
    opt.static_mesh_import_data.auto_generate_collision = False
    opt.static_mesh_import_data.normal_import_method = u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
    t.options = opt
    at.import_asset_tasks([t])
    mesh = lib.load_asset(mesh_path)
    if not mesh:
        u.log_error('mesh import failed ' + gid)
        continue
    # material instance
    mi_path = ROOT + '/Materials/MI_' + gid
    mi = lib.load_asset(mi_path) if lib.does_asset_exist(mi_path) else at.create_asset('MI_' + gid, ROOT + '/Materials', u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    for k, pname in (('BaseColor', 'BaseColorTex'), ('ORM', 'ORMTex'), ('Normal', 'NormalTex'), ('SkinMask', 'SkinMaskTex'), ('Emissive', 'EmissiveTex')):
        if k in tex:
            mel.set_material_instance_texture_parameter_value(mi, pname, tex[k])
    lib.save_loaded_asset(mi)
    mesh.set_material(0, mi)
    # LODs: 100% / 50% / 25% triangles
    ro = u.EditorScriptingMeshReductionOptions()
    ro.reduction_settings = [u.EditorScriptingMeshReductionSettings(1.0, 1.0), u.EditorScriptingMeshReductionSettings(.5, .35),
                             u.EditorScriptingMeshReductionSettings(.25, .12)]
    u.EditorStaticMeshLibrary.set_lods(mesh, ro)
    # sockets (Blender +X = gun's right; Unreal is left-handed, so X is mirrored like the FBX import does)
    for name, loc in rec['sockets_cm'].items():
        if mesh.find_socket(name):
            continue
        s = u.new_object(u.StaticMeshSocket, outer=mesh)
        s.set_editor_property('socket_name', name)
        s.set_editor_property('relative_location', u.Vector(-loc[0], loc[1], loc[2]))
        mesh.add_socket(s)
    lib.save_loaded_asset(mesh)
    report.append({'id': gid, 'lods': mesh.get_num_lods(), 'triangles': [mesh.get_num_triangles(i) for i in range(mesh.get_num_lods())],
                   'textures': sorted(tex), 'sockets': list(rec['sockets_cm'])})
    u.log('WEAPON_V3_IMPORTED ' + gid)
json.dump(report, open(os.path.join(SRC, 'UnrealImportReport_V3.json'), 'w'), indent=2)
lib.save_directory(ROOT)
u.log('WEAPON_V3_IMPORT_COMPLETE %d' % len(report))
