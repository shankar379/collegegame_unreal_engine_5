"""Weapon Library V3 - surfaces, texture bake, previews, export.

Material kinds are procedural "smart materials" (like a Substance Painter base layer):
  base colour + slight colour variation + edge wear on sharp edges + grime in crevices
  + micro detail in the normal map (wood grain, polymer stipple, fine machining scratches).
They are baked once per gun into ordinary textures that Unreal / three.js can use:
  T_<id>_BaseColor.png   sRGB colour (wear and grime painted in)
  T_<id>_ORM.png         R = ambient occlusion, G = roughness, B = metallic (Unreal packing)
  T_<id>_Normal.png      DirectX normal map (Unreal);  T_<id>_Normal_GL.png  OpenGL (Blender/three.js/glTF)
  T_<id>_SkinMask.png    R = where a skin may paint, G = edge wear, B = cavity  (skins reuse wear + AO)
"""
import bpy, math, os
import numpy as np

# kind: base colour A, base colour B (variation), metallic, roughness, edge colour, edge metal, edge rough,
#       wear amount, grime amount, detail ('steel','wood','stipple','smooth','brushed')
KINDS = {
    'bakelite': dict(a=(.032, .017, .011), b=(.018, .010, .007), m=0, r=.40, e=(.09, .045, .025), em=0, er=.48, wear=.3, grime=.4, d='smooth'),
    'steel':        dict(a=(.014, .015, .017), b=(.009, .010, .011), m=.75, r=.42, e=(.50, .50, .51), em=1, er=.24, wear=.9, grime=.6, d='steel'),
    'steel_bright': dict(a=(.30, .30, .31), b=(.22, .22, .23), m=1, r=.30, e=(.62, .62, .63), em=1, er=.18, wear=.5, grime=.7, d='brushed'),
    'blued':        dict(a=(.012, .014, .019), b=(.008, .009, .012), m=.9, r=.32, e=(.45, .46, .48), em=1, er=.2, wear=.8, grime=.5, d='brushed'),
    'alu':          dict(a=(.022, .023, .025), b=(.016, .017, .019), m=.55, r=.5, e=(.55, .56, .58), em=1, er=.25, wear=1., grime=.5, d='steel'),
    'polymer':      dict(a=(.022, .023, .025), b=(.017, .018, .020), m=0, r=.72, e=(.10, .10, .105), em=0, er=.45, wear=.6, grime=.4, d='stipple'),
    'polymer_tan':  dict(a=(.24, .17, .095), b=(.20, .14, .08), m=0, r=.7, e=(.38, .30, .20), em=0, er=.5, wear=.6, grime=.6, d='stipple'),
    'polymer_green': dict(a=(.050, .068, .036), b=(.040, .055, .028), m=0, r=.60, e=(.20, .23, .15), em=0, er=.45, wear=.6, grime=.5, d='stipple'),
    'wood':         dict(a=(.26, .065, .014), b=(.11, .024, .005), m=0, r=.36, e=(.36, .16, .06), em=0, er=.5, wear=.7, grime=.6, d='wood'),
    'wood_dark':    dict(a=(.14, .040, .012), b=(.06, .016, .005), m=0, r=.38, e=(.32, .16, .07), em=0, er=.5, wear=.7, grime=.6, d='wood'),
    'rubber':       dict(a=(.020, .020, .021), b=(.016, .016, .017), m=0, r=.85, e=(.05, .05, .05), em=0, er=.8, wear=.2, grime=.3, d='stipple'),
    'brass':        dict(a=(.55, .36, .10), b=(.45, .28, .07), m=1, r=.30, e=(.85, .65, .30), em=1, er=.15, wear=.6, grime=.7, d='brushed'),
    'stainless':    dict(a=(.40, .40, .41), b=(.37, .37, .38), m=1, r=.30, e=(.62, .62, .63), em=1, er=.16, wear=.4, grime=.7, d='brushed'),
    'chrome':       dict(a=(.62, .62, .64), b=(.55, .55, .57), m=1, r=.14, e=(.75, .75, .77), em=1, er=.1, wear=.2, grime=.5, d='smooth'),
    'ceramic':      dict(a=(.78, .79, .81), b=(.70, .71, .73), m=0, r=.32, e=(.92, .92, .93), em=0, er=.3, wear=.4, grime=.8, d='smooth'),
    'ceramic_red':  dict(a=(.55, .02, .025), b=(.45, .015, .02), m=0, r=.3, e=(.8, .2, .2), em=0, er=.3, wear=.4, grime=.6, d='smooth'),
    'glow_cyan':    dict(a=(.10, .80, 1.0), b=(.10, .80, 1.0), m=0, r=.2, e=(.10, .80, 1.0), em=0, er=.2, wear=0, grime=0, d='smooth', glow=1),
    'glow_red':     dict(a=(1.0, .08, .05), b=(1.0, .08, .05), m=0, r=.2, e=(1.0, .08, .05), em=0, er=.2, wear=0, grime=0, d='smooth', glow=1),
    'blade':        dict(a=(.55, .56, .58), b=(.48, .49, .51), m=1, r=.18, e=(.75, .75, .76), em=1, er=.12, wear=.3, grime=.6, d='brushed'),
    'lens':         dict(a=(.010, .030, .045), b=(.010, .030, .045), m=0, r=.04, e=(.010, .030, .045), em=0, er=.04, wear=0, grime=0, d='smooth'),
    'bore':         dict(a=(.004, .004, .005), b=(.004, .004, .005), m=.5, r=.6, e=(.01, .01, .01), em=.5, er=.6, wear=0, grime=0, d='smooth'),
}

UV0 = 'UV0'


def _n(nt, typ, x=0, y=0, **kw):
    n = nt.nodes.new(typ)
    n.location = (x, y)
    for k, v in kw.items():
        setattr(n, k, v)
    return n


def _inp(n, ident):
    for s in n.inputs:
        if s.identifier == ident:
            return s
    raise KeyError(ident)


def _out(n, ident):
    for s in n.outputs:
        if s.identifier == ident:
            return s
    raise KeyError(ident)


def _mixc(nt, fac, a, b):
    m = _n(nt, 'ShaderNodeMix', data_type='RGBA')
    L = nt.links.new
    if isinstance(fac, (int, float)):
        _inp(m, 'Factor_Float').default_value = fac
    else:
        L(fac, _inp(m, 'Factor_Float'))
    for sock, val in ((_inp(m, 'A_Color'), a), (_inp(m, 'B_Color'), b)):
        if isinstance(val, tuple):
            sock.default_value = (*val, 1)
        else:
            L(val, sock)
    return _out(m, 'Result_Color')


def _mixf(nt, fac, a, b):
    m = _n(nt, 'ShaderNodeMix', data_type='FLOAT')
    L = nt.links.new
    if isinstance(fac, (int, float)):
        _inp(m, 'Factor_Float').default_value = fac
    else:
        L(fac, _inp(m, 'Factor_Float'))
    for sock, val in ((_inp(m, 'A_Float'), a), (_inp(m, 'B_Float'), b)):
        if isinstance(val, (int, float)):
            sock.default_value = val
        else:
            L(val, sock)
    return _out(m, 'Result_Float')


def _math(nt, op, a, b=None, clamp=False):
    m = _n(nt, 'ShaderNodeMath', operation=op, use_clamp=clamp)
    for sock, val in zip(m.inputs, (a, b)):
        if val is None:
            continue
        if isinstance(val, (int, float)):
            sock.default_value = val
        else:
            nt.links.new(val, sock)
    return m.outputs[0]


def _maprange(nt, v, a, b, c=0., d=1.):
    m = _n(nt, 'ShaderNodeMapRange', clamp=True)
    nt.links.new(v, m.inputs['Value'])
    m.inputs['From Min'].default_value = a
    m.inputs['From Max'].default_value = b
    m.inputs['To Min'].default_value = c
    m.inputs['To Max'].default_value = d
    return m.outputs['Result']


def _noise(nt, vec, scale, detail=4, rough=.55, distort=0., stretch=None):
    if stretch:
        mp = _n(nt, 'ShaderNodeMapping')
        mp.inputs['Scale'].default_value = stretch
        nt.links.new(vec, mp.inputs['Vector'])
        vec = mp.outputs['Vector']
    t = _n(nt, 'ShaderNodeTexNoise')
    nt.links.new(vec, t.inputs['Vector'])
    t.inputs['Scale'].default_value = scale
    t.inputs['Detail'].default_value = detail
    t.inputs['Roughness'].default_value = rough
    t.inputs['Distortion'].default_value = distort
    return t.outputs['Fac']


# ---------------------------------------------------------------- stage 1: measure edges + cavities
def measure_material():
    """Bakes R = sharp-edge factor (for edge wear), G = ambient occlusion. Raytraced once per gun."""
    m = bpy.data.materials.new('WV3_Measure')
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    geo = _n(nt, 'ShaderNodeNewGeometry')
    bev = _n(nt, 'ShaderNodeBevel', samples=6)
    bev.inputs['Radius'].default_value = 0.16
    dot = _n(nt, 'ShaderNodeVectorMath', operation='DOT_PRODUCT')
    nt.links.new(geo.outputs['Normal'], dot.inputs[0])
    nt.links.new(bev.outputs['Normal'], dot.inputs[1])
    edge = _maprange(nt, _math(nt, 'SUBTRACT', 1.0, dot.outputs['Value']), 0.004, 0.06)
    ao = _n(nt, 'ShaderNodeAmbientOcclusion', samples=6, only_local=False)
    ao.inputs['Distance'].default_value = 2.5
    comb = _n(nt, 'ShaderNodeCombineColor')
    nt.links.new(edge, comb.inputs[0])
    nt.links.new(ao.outputs['AO'], comb.inputs[1])
    em = _n(nt, 'ShaderNodeEmission')
    nt.links.new(comb.outputs[0], em.inputs['Color'])
    out = _n(nt, 'ShaderNodeOutputMaterial')
    nt.links.new(em.outputs[0], out.inputs['Surface'])
    img = _n(nt, 'ShaderNodeTexImage', name='BAKE_TARGET')
    nt.nodes.active = img
    return m


# ---------------------------------------------------------------- stage 2: smart materials
def smart_material(kind, meas_img):
    p = KINDS[kind]
    m = bpy.data.materials.new('WV3_' + kind)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    L = nt.links.new
    tc = _n(nt, 'ShaderNodeTexCoord')
    obj = tc.outputs['Object']
    uv = _n(nt, 'ShaderNodeUVMap', uv_map=UV0)
    meas = _n(nt, 'ShaderNodeTexImage', image=meas_img, interpolation='Linear')
    L(uv.outputs['UV'], meas.inputs['Vector'])
    sep = _n(nt, 'ShaderNodeSeparateColor')
    L(meas.outputs['Color'], sep.inputs[0])
    edge, ao = sep.outputs[0], sep.outputs[1]

    # edge wear: sharp edges, broken up by noise so it looks chipped, not outlined
    brk = _noise(nt, obj, 1.6, 6, .65)
    brk2 = _maprange(nt, brk, .38, .62)
    wear = _math(nt, 'MULTIPLY', _maprange(nt, _math(nt, 'MULTIPLY', edge, brk2), .15, .55), p['wear'], clamp=True)
    # grime: crevices (low AO) get darker and rougher
    grime = _math(nt, 'MULTIPLY', _maprange(nt, ao, .95, .35), p['grime'], clamp=True)

    var = _noise(nt, obj, .35, 5, .6)
    if p['d'] == 'wood':
        mp = _n(nt, 'ShaderNodeMapping')
        mp.inputs['Scale'].default_value = (1.0, .06, 1.0)
        L(obj, mp.inputs['Vector'])
        wv = _n(nt, 'ShaderNodeTexWave', wave_type='RINGS')
        L(mp.outputs['Vector'], wv.inputs['Vector'])
        wv.inputs['Scale'].default_value = .9
        wv.inputs['Distortion'].default_value = 6
        wv.inputs['Detail'].default_value = 4
        wv.inputs['Detail Scale'].default_value = 1.5
        grain = _maprange(nt, wv.outputs['Fac'], .25, .95)
        fine = _noise(nt, obj, 9, 3, .5, stretch=(1, .04, 1))
        var = _math(nt, 'MULTIPLY', _math(nt, 'ADD', grain, _math(nt, 'MULTIPLY', fine, .35)), .75, clamp=True)
    base = _mixc(nt, var, p['a'], p['b'])
    col = _mixc(nt, wear, base, p['e'])
    dark = _n(nt, 'ShaderNodeMix', data_type='RGBA', blend_type='MULTIPLY')
    L(grime, _inp(dark, 'Factor_Float'))
    L(col, _inp(dark, 'A_Color'))
    _inp(dark, 'B_Color').default_value = (.35, .32, .28, 1)
    col = _out(dark, 'Result_Color')

    rvar = _noise(nt, obj, 2.2, 4, .6)
    rough = _math(nt, 'ADD', _mixf(nt, wear, p['r'], p['er']), _math(nt, 'MULTIPLY', _math(nt, 'SUBTRACT', rvar, .5), .14))
    rough = _math(nt, 'ADD', rough, _math(nt, 'MULTIPLY', grime, .15), clamp=True)
    metal = _mixf(nt, wear, p['m'], p['em'])

    # micro detail for the normal map
    height = None
    strength = 0
    if p['d'] == 'stipple':
        height, strength = _noise(nt, obj, 14, 2, .5), (.8 if kind == 'rubber' else .35)
    elif p['d'] == 'wood':
        height, strength = _noise(nt, obj, 6, 3, .5, stretch=(1, .03, 1)), .12
    elif p['d'] == 'steel':
        height, strength = _noise(nt, obj, 5, 6, .7), .06
    elif p['d'] == 'brushed':
        height, strength = _noise(nt, obj, 30, 2, .5, stretch=(1, .02, 1)), .05

    bsdf = _n(nt, 'ShaderNodeBsdfPrincipled', name='BSDF')
    L(col, bsdf.inputs['Base Color'])
    L(rough, bsdf.inputs['Roughness'])
    L(metal, bsdf.inputs['Metallic'])
    if height is not None:
        bump = _n(nt, 'ShaderNodeBump')
        bump.inputs['Strength'].default_value = strength
        bump.inputs['Distance'].default_value = .02
        L(height, bump.inputs['Height'])
        L(bump.outputs['Normal'], bsdf.inputs['Normal'])
    out = _n(nt, 'ShaderNodeOutputMaterial', name='MAIN_OUT')
    L(bsdf.outputs[0], out.inputs['Surface'])

    # bake helpers: an emission output we can point at any channel
    skin = _n(nt, 'ShaderNodeAttribute', attribute_name='skin', attribute_type='GEOMETRY')
    em = _n(nt, 'ShaderNodeEmission', name='BAKE_EMIT')
    bout = _n(nt, 'ShaderNodeOutputMaterial', name='BAKE_OUT')
    L(em.outputs[0], bout.inputs['Surface'])
    out.is_active_output = True
    img = _n(nt, 'ShaderNodeTexImage', name='BAKE_TARGET')
    nt.nodes.active = img
    m['ch'] = 0
    emit = _mixc(nt, float(p.get('glow', 0)), (0., 0., 0.), col)
    CH[m.name] = {'color': col, 'rough': rough, 'metal': metal, 'wear': wear, 'skin': skin.outputs['Fac'], 'grime': grime, 'emit': emit}
    return m


CH = {}


def _set_target(mats, img):
    for m in mats:
        t = m.node_tree.nodes['BAKE_TARGET']
        t.image = img
        m.node_tree.nodes.active = t


def _emit_channel(mats, ch):
    for m in mats:
        nt = m.node_tree
        if ch is None:
            nt.nodes['MAIN_OUT'].is_active_output = True
            continue
        em = nt.nodes['BAKE_EMIT']
        for l in list(em.inputs['Color'].links):
            nt.links.remove(l)
        nt.links.new(CH[m.name][ch], em.inputs['Color'])
        nt.nodes['BAKE_OUT'].is_active_output = True


def _img(name, size, data, h=None):
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    im = bpy.data.images.new(name, size, h or size, alpha=False, float_buffer=False, is_data=data)
    if data:
        im.colorspace_settings.name = 'Non-Color'
    return im


def _px(im):
    a = np.empty(im.size[0] * im.size[1] * 4, np.float32)
    im.pixels.foreach_get(a)
    return a.reshape(im.size[1], im.size[0], 4)


def _save(name, arr, path, data):
    h, w = arr.shape[:2]
    im = _img(name, w, data, h)
    im.pixels.foreach_set(arr.astype(np.float32).ravel())
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()
    return im


def bake_gun(o, gid, outdir, size=2048, log=print):
    """Bake every texture for one joined gun mesh. Returns dict of saved image paths and the loaded images."""
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.render.bake.margin = 16
    sc.render.bake.margin_type = 'EXTEND'
    sc.render.bake.use_clear = True
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    tex = os.path.join(outdir, 'Textures')
    os.makedirs(tex, exist_ok=True)

    # stage 1: edges + AO with the measure material on every slot
    originals = [s.material for s in o.material_slots]
    mm = measure_material()
    meas = _img('MEAS_' + gid, max(512, size // 2), True)  # soft maps: half resolution is plenty
    mm.node_tree.nodes['BAKE_TARGET'].image = meas
    for s in o.material_slots:
        s.material = mm
    sc.cycles.samples = 8
    log('  bake: edges + ambient occlusion')
    bpy.ops.object.bake(type='EMIT')
    for s, m in zip(o.material_slots, originals):
        s.material = m
    bpy.data.materials.remove(mm)

    # stage 2: smart materials read the measurement; flat passes need only 1-2 samples
    mats = []
    for s in o.material_slots:
        kind = s.material['kind']
        nm = smart_material(kind, meas)
        bpy.data.materials.remove(s.material)
        s.material = nm
        nm['kind'] = kind
        mats.append(nm)
    sc.cycles.samples = 2
    res = {}
    glowing = any(KINDS[s.material['kind']].get('glow') for s in o.material_slots)
    passes = [('color', False), ('rough', True), ('metal', True), ('wear', True), ('skin', True), ('grime', True)] + ([('emit', False)] if glowing else [])
    for ch, data in passes:
        im = _img('B_%s_%s' % (ch, gid), size, data)
        _set_target(mats, im)
        _emit_channel(mats, ch)
        log('  bake: ' + ch)
        bpy.ops.object.bake(type='EMIT')
        res[ch] = _px(im)
    _emit_channel(mats, None)
    im = _img('B_normal_' + gid, size, True)
    _set_target(mats, im)
    log('  bake: normal')
    bpy.ops.object.bake(type='NORMAL', normal_space='TANGENT', uv_layer=UV0)
    res['normal'] = _px(im)
    ao = _px(meas)[..., 1]
    if ao.shape != res['color'].shape[:2]:
        k = res['color'].shape[0] // ao.shape[0]
        ao = np.kron(ao, np.ones((k, k), np.float32))

    one = np.ones_like(ao)
    files = {}
    rgb = lambda r, g, b: np.stack([r, g, b, one], -1)
    files['BaseColor'] = _save('T_%s_BaseColor' % gid, res['color'], os.path.join(tex, 'T_%s_BaseColor.png' % gid), False)
    aoc = np.clip(ao * .85 + .15, 0, 1)
    files['ORM'] = _save('T_%s_ORM' % gid, rgb(aoc, res['rough'][..., 0], res['metal'][..., 0]), os.path.join(tex, 'T_%s_ORM.png' % gid), True)
    nrm = res['normal'].copy()
    files['Normal_GL'] = _save('T_%s_Normal_GL' % gid, nrm, os.path.join(tex, 'T_%s_Normal_GL.png' % gid), True)
    nrm[..., 1] = 1 - nrm[..., 1]
    _save('T_%s_Normal' % gid, nrm, os.path.join(tex, 'T_%s_Normal.png' % gid), True)
    if glowing:
        files['Emissive'] = _save('T_%s_Emissive' % gid, res['emit'], os.path.join(tex, 'T_%s_Emissive.png' % gid), False)
    files['SkinMask'] = _save('T_%s_SkinMask' % gid, rgb(res['skin'][..., 0], res['wear'][..., 0], 1 - ao), os.path.join(tex, 'T_%s_SkinMask.png' % gid), True)
    # done with the smart materials: one clean game material that uses only the baked textures
    for m in mats:
        bpy.data.materials.remove(m)
    o.data.materials.clear()
    o.data.materials.append(game_material(gid, files))
    for p in o.data.polygons:
        p.material_index = 0
    for k in list(bpy.data.images):
        if k.name.startswith(('B_', 'MEAS_')):
            bpy.data.images.remove(k)
    return files


def game_material(gid, files, skin=None):
    """The material the game uses: baked BaseColor + ORM + Normal. With skin=(image, metal, rough)
    the skin image is painted through the SkinMask on UV1 (side projection) - this is exactly
    what the Unreal material does at runtime, so the previews show real skin behaviour."""
    m = bpy.data.materials.new('M_' + gid + ('' if skin is None else '_' + skin[0].name))
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    L = nt.links.new
    uv = _n(nt, 'ShaderNodeUVMap', uv_map=UV0)
    def tex(im, data):
        t = _n(nt, 'ShaderNodeTexImage', image=im)
        if data:
            im.colorspace_settings.name = 'Non-Color'
        L(uv.outputs['UV'], t.inputs['Vector'])
        return t
    bc = tex(files['BaseColor'], False)
    orm = tex(files['ORM'], True)
    nrm = tex(files['Normal_GL'], True)
    sep = _n(nt, 'ShaderNodeSeparateColor')
    L(orm.outputs['Color'], sep.inputs[0])
    nm = _n(nt, 'ShaderNodeNormalMap', uv_map=UV0)
    L(nrm.outputs['Color'], nm.inputs['Color'])
    bsdf = _n(nt, 'ShaderNodeBsdfPrincipled')
    col, rough, metal = bc.outputs['Color'], sep.outputs[1], sep.outputs[2]
    if skin is not None:
        sim, smet, srough = skin
        mask = tex(files['SkinMask'], True)
        msep = _n(nt, 'ShaderNodeSeparateColor')
        L(mask.outputs['Color'], msep.inputs[0])
        uv1 = _n(nt, 'ShaderNodeUVMap', uv_map='UV1')
        st = _n(nt, 'ShaderNodeTexImage', image=sim)
        L(uv1.outputs['UV'], st.inputs['Vector'])
        # skin keeps the gun's wear (worn edges show metal) and cavities (AO)
        aoed = _n(nt, 'ShaderNodeMix', data_type='RGBA', blend_type='MULTIPLY')
        L(_maprange(nt, msep.outputs[2], 0, 1, 0, .7), _inp(aoed, 'Factor_Float'))
        L(st.outputs['Color'], _inp(aoed, 'A_Color'))
        _inp(aoed, 'B_Color').default_value = (.25, .25, .25, 1)
        worn = _mixc(nt, _math(nt, 'MULTIPLY', msep.outputs[1], .8), _out(aoed, 'Result_Color'), (.55, .55, .56))
        col = _mixc(nt, msep.outputs[0], col, worn)
        rough = _mixf(nt, msep.outputs[0], rough, srough)
        metal = _mixf(nt, msep.outputs[0], metal, _mixf(nt, msep.outputs[1], smet, 1.0))
        glow = _n(nt, 'ShaderNodeSeparateColor')
        L(st.outputs['Color'], glow.inputs[0])
    L(col, bsdf.inputs['Base Color'])
    L(rough, bsdf.inputs['Roughness'])
    L(metal, bsdf.inputs['Metallic'])
    L(nm.outputs['Normal'], bsdf.inputs['Normal'])
    if 'Emissive' in files:
        et = tex(files['Emissive'], False)
        L(et.outputs['Color'], bsdf.inputs['Emission Color'])
        bsdf.inputs['Emission Strength'].default_value = 6.0
    out = _n(nt, 'ShaderNodeOutputMaterial')
    L(bsdf.outputs[0], out.inputs['Surface'])
    return m


# ---------------------------------------------------------------- example skins (generated, side-projected on UV1)
def make_skins(outdir, size=1024):
    """Three example skins in the Free Fire style. They map through UV1 (side view of the gun),
    so ONE image works on every gun. Artists can paint over T_<id>_SkinTemplate.png for a custom one."""
    d = os.path.join(outdir, 'Textures', 'Skins')
    os.makedirs(d, exist_ok=True)
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32) / size
    one = np.ones((size, size), np.float32)
    out = {}
    # 1) Neon Hex - lime body, dark hex cells, cyan glowing seams (reference: green SCAR skin)
    s = 22.0
    q = (xx * s * 2 / math.sqrt(3))
    r = yy * s - q * .5
    def hexdist(qx, ry):
        x, z = qx, ry
        y = -x - z
        rx, ry_, rz = np.round(x), np.round(y), np.round(z)
        dx, dy, dz = np.abs(rx - x), np.abs(ry_ - y), np.abs(rz - z)
        fx = np.where((dx > dy) & (dx > dz), -ry_ - rz, rx)
        fz = np.where(~((dx > dy) & (dx > dz)) & ~(dy > dz), -rx - ry_, rz)
        fy = -fx - fz
        return np.maximum(np.maximum(np.abs(x - fx), np.abs(y - fy)), np.abs(z - fz))
    h = hexdist(q, r)
    line = np.clip((h - .40) / .08, 0, 1)
    band_ = (np.sin(xx * 9 + yy * 3) > .6).astype(np.float32)
    base = np.stack([.62 * one, .85 * one, .05 * one], -1)
    dark = np.stack([.04 * one, .05 * one, .03 * one], -1)
    cyan = np.stack([.1 * one, .9 * one, 1. * one], -1)
    col = base * (1 - band_[..., None] * .55) + dark * band_[..., None] * .55
    col = col * (1 - line[..., None]) + (cyan * band_[..., None] + dark * (1 - band_[..., None])) * line[..., None]
    out['NeonHex'] = (col, .15, .38)
    # 2) Gold Relic - polished gold with red lacquer panels and engraved lines (reference: gold MP40)
    stripes = (np.abs(((yy * 7) % 1) - .5) < .30).astype(np.float32)
    panel = stripes * (np.abs(((xx * 5 + .25) % 1) - .5) < .38)
    eng = (np.abs(np.sin(xx * 140) * np.sin(yy * 60)) > .985).astype(np.float32)
    gold = np.stack([1.0 * one, .62 * one, .18 * one], -1)
    red = np.stack([.55 * one, .03 * one, .03 * one], -1)
    col = gold * (1 - panel[..., None]) + red * panel[..., None]
    col = col * (1 - eng[..., None] * .5)
    out['GoldRelic'] = (col, .95, .25)
    # 3) Arctic Circuit - white ceramic with navy and red circuit lines (reference: white VSS skin)
    cx = np.abs(((xx * 12) % 1) - .5) < .03
    cy = np.abs(((yy * 9) % 1) - .5) < .03
    gate = ((np.floor(xx * 12) + np.floor(yy * 9)) % 3 == 0)
    circuit = ((cx | cy) & gate).astype(np.float32)
    redl = (np.abs(yy - .55) < .012).astype(np.float32)
    white = np.stack([.86 * one, .88 * one, .9 * one], -1)
    navy = np.stack([.05 * one, .08 * one, .22 * one], -1)
    redc = np.stack([.85 * one, .05 * one, .05 * one], -1)
    col = white * (1 - circuit[..., None]) + navy * circuit[..., None]
    col = col * (1 - redl[..., None]) + redc * redl[..., None]
    out['ArcticCircuit'] = (col, .35, .3)
    # 4) Royal Gold - polished gold with engraved filigree scrollwork and deep red lacquer bands
    X, Y = xx * 2 * math.pi, yy * 2 * math.pi
    scroll = np.sin(X * 9 + 1.6 * np.sin(Y * 7)) * np.sin(Y * 9 + 1.6 * np.sin(X * 7))
    curl = np.sin(np.hypot(((xx * 6) % 1) - .5, ((yy * 6) % 1) - .5) * 40)
    engr = ((np.abs(scroll) < .09) | (np.abs(curl) < .08)).astype(np.float32)
    bands = ((np.abs(yy - .30) < .035) | (np.abs(yy - .70) < .035)).astype(np.float32)
    trim = ((np.abs(yy - .30) < .045) | (np.abs(yy - .70) < .045)).astype(np.float32) - bands
    gold = np.stack([1.0 * one, .70 * one, .26 * one], -1)
    dark = np.stack([.30 * one, .17 * one, .04 * one], -1)
    lac = np.stack([.42 * one, .01 * one, .02 * one], -1)
    col = gold * (1 - engr[..., None] * .75) + dark * engr[..., None] * .75
    col = col * (1 - bands[..., None]) + lac * bands[..., None]
    col = col * (1 - trim[..., None]) + np.stack([1.0 * one, .85 * one, .45 * one], -1) * trim[..., None]
    out['RoyalGold'] = (col, 1.0, .2)
    ims = {}
    for name, (c, met, rough) in out.items():
        arr = np.concatenate([c, one[..., None]], -1)[::-1]
        im = _save('SKIN_' + name, arr, os.path.join(d, 'T_Skin_%s.png' % name), False)
        ims[name] = (im, met, rough)
    return ims


# ---------------------------------------------------------------- preview renders from every direction
def _studio():
    sc = bpy.context.scene
    w = sc.world or bpy.data.worlds.new('W')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    # dim studio surroundings: a soft vertical gradient (lighter above) - keeps black plastic black,
    # while metal still picks up the three area lights as clean highlights
    geo = _n(nt, 'ShaderNodeTexCoord')
    sep = _n(nt, 'ShaderNodeSeparateXYZ')
    nt.links.new(geo.outputs['Generated'], sep.inputs[0])  # for a world this is the view direction
    grad = _n(nt, 'ShaderNodeValToRGB')       # dark floor, soft bright softbox band at the horizon, bright ceiling
    grad.color_ramp.elements[0].color = (.004, .0045, .005, 1)
    grad.color_ramp.elements[1].position = .62
    grad.color_ramp.elements[1].color = (.30, .31, .33, 1)
    e = grad.color_ramp.elements.new(.48)
    e.color = (.20, .21, .22, 1)
    e = grad.color_ramp.elements.new(1.0)
    e.color = (.55, .56, .58, 1)
    zr = _maprange(nt, sep.outputs['Z'], -1.0, 1.0)      # world direction z (-1 down .. +1 up) -> 0..1
    nt.links.new(zr, grad.inputs[0])
    bg = _n(nt, 'ShaderNodeBackground')
    nt.links.new(grad.outputs['Color'], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = 1.0
    lp = _n(nt, 'ShaderNodeLightPath')
    flat = _n(nt, 'ShaderNodeBackground')
    flat.inputs['Color'].default_value = (.022, .026, .034, 1)
    mix = _n(nt, 'ShaderNodeMixShader')
    nt.links.new(lp.outputs['Is Camera Ray'], mix.inputs[0])
    nt.links.new(bg.outputs[0], mix.inputs[1])
    nt.links.new(flat.outputs[0], mix.inputs[2])
    out = _n(nt, 'ShaderNodeOutputWorld')
    nt.links.new(mix.outputs[0], out.inputs['Surface'])
    for name, loc, energy, size in (('Key', (60, -40, 70), 1.3e5, 60), ('Rim', (-70, 60, 40), 1.6e5, 50), ('Fill', (40, -60, -30), .45e5, 80), ('Top', (0, 10, 120), .8e5, 90)):
        if name in bpy.data.objects:
            continue
        ld = bpy.data.lights.new(name, 'AREA')
        ld.energy = energy
        ld.size = size
        lo = bpy.data.objects.new(name, ld)
        sc.collection.objects.link(lo)
        lo.location = loc
        lo.rotation_euler = (-Vector(loc)).to_track_quat('-Z', 'Y').to_euler()


from mathutils import Vector


def _cam(name):
    if name in bpy.data.objects:
        return bpy.data.objects[name]
    cd = bpy.data.cameras.new(name)
    c = bpy.data.objects.new(name, cd)
    bpy.context.scene.collection.objects.link(c)
    return c


VIEWS = [  # name, direction the camera looks FROM (x,y,z), orthographic?
    ('right', (1, 0, 0), True), ('left', (-1, 0, 0), True), ('top', (0, 0, 1), True),
    ('front34', (.75, .85, .35), False), ('rear34', (-.8, -.75, .45), False), ('muzzle', (.15, 1, .12), False),
]


def render_views(o, path_png, res=(1000, 560), samples=20, views=VIEWS, log=print):
    sc = bpy.context.scene
    _studio()
    sc.render.engine = 'CYCLES'
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    bb = [Vector(c) for c in o.bound_box]
    mn = Vector((min(v.x for v in bb), min(v.y for v in bb), min(v.z for v in bb)))
    mx = Vector((max(v.x for v in bb), max(v.y for v in bb), max(v.z for v in bb)))
    ctr = (mn + mx) / 2 + o.location
    size = (mx - mn)
    tiles = []
    tmp = []
    for name, d, ortho in views:
        cam = _cam('CAM_' + name)
        d = Vector(d).normalized()
        dist = max(size) * 2.4
        cam.location = ctr + d * dist
        up = 'Y' if abs(d.z) < .99 else 'Y'
        cam.rotation_euler = (-d).to_track_quat('-Z', 'Y' if abs(d.z) < .99 else 'Y').to_euler()
        if abs(d.z) > .99:  # top view: muzzle to the right
            cam.rotation_euler = (0, 0, math.pi / 2)
        cam.data.clip_end = dist * 4
        if ortho:
            cam.data.type = 'ORTHO'
            span = max(size.y, (size.z if abs(d.z) < .99 else size.x) * res[0] / res[1])
            cam.data.ortho_scale = span * 1.12
        else:
            cam.data.type = 'PERSP'
            cam.data.lens = 50
            cam.location = ctr + d * (max(size) * 1.9)
        sc.camera = cam
        p = path_png[:-4] + '_' + name + '.png'
        sc.render.filepath = p
        log('  render ' + name)
        bpy.ops.render.render(write_still=True)
        tmp.append(p)
        if name == 'right':  # exact silhouette for the shape-match tool
            sc.render.film_transparent = True
            sc.cycles.samples, keep = 1, sc.cycles.samples
            sc.render.image_settings.color_mode = 'RGBA'
            sc.render.filepath = p[:-4] + '_alpha.png'
            bpy.ops.render.render(write_still=True)
            sc.render.film_transparent = False
            sc.render.image_settings.color_mode = 'RGB'
            sc.cycles.samples = keep
    # contact sheet 3 x 2
    ims = []
    for p in tmp:
        im = bpy.data.images.load(p)
        ims.append(_px(im)[::-1])
        bpy.data.images.remove(im)
    h, w = ims[0].shape[:2]
    cols = min(3, len(ims))
    rows = (len(ims) + cols - 1) // cols
    sheet = np.zeros((rows * h, cols * w, 4), np.float32)
    sheet[..., 3] = 1
    for i, a in enumerate(ims):
        r, c = divmod(i, cols)
        sheet[r * h:(r + 1) * h, c * w:(c + 1) * w] = a
    _save('SHEET', sheet[::-1], path_png, False)
    for p in tmp:
        if not p.endswith('_right.png'):
            os.remove(p)
    return path_png
