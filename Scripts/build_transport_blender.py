"""Blender background build: original reference-inspired cargo transport, metres, +X nose."""
import bpy, math, json, os
from mathutils import Vector
OUT = os.environ.get('TRANSPORT_OUTPUT',os.path.join(os.path.dirname(os.path.dirname(__file__)),'ArtSource','Transport'))
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
def mat(name, color, metal=0):
    m=bpy.data.materials.new(name); m.diffuse_color=(*color,1); m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=(*color,1); p.inputs['Metallic'].default_value=metal; p.inputs['Roughness'].default_value=.65
    return m
skin=mat('Exterior_Replaceable',(.32,.38,.29),.3)
tex=skin.node_tree.nodes.new('ShaderNodeTexImage'); tex.image=bpy.data.images.load(os.path.join(OUT,'T_Transport_Panels.png')); tex.image.pack()
mix=skin.node_tree.nodes.new('ShaderNodeMixRGB'); mix.blend_type='MULTIPLY'; mix.inputs[0].default_value=1; mix.inputs[2].default_value=(.32,.38,.29,1)
skin.node_tree.links.new(tex.outputs['Color'],mix.inputs[1]); skin.node_tree.links.new(mix.outputs[0],skin.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
dark=mat('Mechanical',(.035,.05,.06),.5)
inside=mat('Interior',(.24,.27,.25),.2)
parts=[]
def mesh(name,v,f,m=skin):
    me=bpy.data.meshes.new(name); me.from_pydata(v,[],f); me.update()
    o=bpy.data.objects.new(name,me); bpy.context.collection.objects.link(o); o.data.materials.append(m); parts.append(o); return o
def box(name,loc,scale,m=skin):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc); o=bpy.context.object; o.name=name; o.scale=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True); o.data.materials.append(m); parts.append(o); return o
def loft(name,rings,n=32,m=skin,cap=True):
    v=[(x,ry*math.cos(2*math.pi*j/n),z+rz*math.sin(2*math.pi*j/n)) for x,ry,rz,z in rings for j in range(n)]
    f=[(i*n+j,i*n+(j+1)%n,(i+1)*n+(j+1)%n,(i+1)*n+j) for i in range(len(rings)-1) for j in range(n)]
    if cap: f += [tuple(reversed(range(n))),tuple((len(rings)-1)*n+j for j in range(n))]
    o=mesh(name,v,f,m)
    for p in o.data.polygons:p.use_smooth=True
    return o
# Hollow cargo barrel, rear aperture retained for the animated ramp.
loft('Cargo shell',[(-8,1.8,1.9,.3),(-5,2.05,2.2,0),(0,2.05,2.2,0),(6,2,2.1,0),(9,1.8,1.8,0),(11,1.2,1.25,-.2),(12,.35,.6,-.3)],cap=False)
loft('Nose cap',[(12,.35,.6,-.3),(12.5,.05,.1,-.3)],cap=True)
loft('Interior liner',[(-7.95,1.72,1.82,.3),(-5,1.97,2.12,0),(6,1.92,2.02,0)],m=inside,cap=False)
# Reverse inner surface normals so the cabin is visible with single-sided shading.
o=parts[-1]
for p in o.data.polygons: pass
bpy.context.view_layer.objects.active=o; o.select_set(True)
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.mesh.flip_normals(); bpy.ops.object.mode_set(mode='OBJECT'); o.select_set(False)
box('Cargo deck',(-1,0,-1.48),(14,2.9,.18),inside)
loft('Forward bulkhead',[(5.9,1.89,1.99,0),(6,1.89,1.99,0)],m=inside)
# Swept tapered airfoils with actual thickness.
def wing(name,root,tip,chord0,chord1,z0,z1,sign=1):
    x0,y0=root; x1,y1=tip
    v=[(x,y*sign,z+dz) for x,y,z,c in [(x0,y0,z0,chord0),(x1,y1,z1,chord1)] for x,dz in [(x+c*.5,0),(x+c*.15,.22),(x-c*.5,0),(x+c*.15,-.12)]]
    faces=[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)]
    return mesh(name,v,[tuple(reversed(f)) for f in faces] if sign<0 else faces)
for s in [-1,1]:
    wing('Main wing',(1,1.6),(-1,20),5,2.2,1.5,2,s)
    wing('Tailplane',(-15,0),(-16.3,7.2),3.6,1.5,1.55,1.75,s)
# C-130 style upswept tail: continues the fuselage at full width, keeps a level roofline,
# and lifts the belly up to the tail cone. The underside stays open so the ramp can swing through.
def tail_arc(name,rings,n=24,a0=-20,a1=200,m=skin,flip=False):
    angles=[math.radians(a0+(a1-a0)*j/n) for j in range(n+1)]
    v=[(x,ry*math.cos(t),z+rz*math.sin(t)) for x,ry,rz,z in rings for t in angles]
    k=n+1; f=[(i*k+j,i*k+j+1,(i+1)*k+j+1,(i+1)*k+j) for i in range(len(rings)-1) for j in range(n)]
    o=mesh(name,v,[tuple(reversed(q)) for q in f] if flip else f,m)
    for p in o.data.polygons:p.use_smooth=True
    return o
tail_rings=[(-8,1.8,1.9,.3),(-10,1.62,1.52,.68),(-12,1.35,1.15,1.05),(-14,1.0,.8,1.4),(-16,.6,.5,1.7),(-17.3,.25,.25,1.95),(-17.7,.03,.03,2.05)]
tail_arc('Tail upsweep',tail_rings)
tail_arc('Tail upsweep liner',[(x,ry*.96,rz*.96,z) for x,ry,rz,z in tail_rings],m=inside,flip=True)
# Tall swept fin with a thinner tip, rooted into the tail roof.
mesh('Vertical stabilizer',[(-11.5,-.16,1.9),(-17.4,-.16,1.9),(-17.6,-.07,8.4),(-15.4,-.07,8.4),(-11.5,.16,1.9),(-17.4,.16,1.9),(-17.6,.07,8.4),(-15.4,.07,8.4)],[(0,3,2,1),(4,5,6,7),(0,4,7,3),(3,7,6,2),(2,6,5,1),(1,5,4,0)])
for y in [-12,-6,6,12]:
    o=loft('Engine',[(-.5,.6,.65,0),(2,.75,.8,0),(4,.55,.6,0),(4.5,.25,.3,0)],24)
    o.location=(0,y,1.1)
    o=loft('Engine intake',[(3.7,.56,.61,0),(3.9,.56,.61,0)],24,dark); o.location=(0,y,1.1)
# Cockpit glazing panels wrap around the forward canopy.
for s in [-1,1]:
    mesh('Cockpit glass',[(9,s*1.76,.6),(10.8,s*1.25,.5),(10.2,s*1.22,1.05),(8.8,s*1.5,1.3)],[(0,1,2,3)],dark)
    for x in range(-6,6,2):
        box('Interior rib',(x,s*1.35,.1),(.09,.12,2.7),inside)
    box('Bench',(-1,s*1.3,-.95),(10,.55,.12),inside)
# Join body into three material sections.
def join(name,objects):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]; bpy.ops.object.join(); o=bpy.context.object; o.name=name
    bpy.context.scene.cursor.location=(0,0,0); bpy.ops.object.origin_set(type='ORIGIN_CURSOR'); return o
body=join('SM_TransportBody',parts[:]); parts=[]
# Ramp local origin is the real bottom hinge; closed slab rises 3.6m from it.
ramp=box('SM_TransportRamp',(0,0,1.8),(.16,3.4,3.6));
bpy.context.scene.cursor.location=(0,0,0); bpy.ops.object.origin_set(type='ORIGIN_CURSOR'); ramp.location=(-8,0,-1.5)
# One prop mesh is instanced four times at runtime.
parts=[]
for a in range(4):
    o=box('Blade',(0,0,0),(.12,.25,2.0),dark)
    angle=a*math.pi/2; o.location=(0,math.sin(angle)*1.15,math.cos(angle)*1.15); o.rotation_euler.x=-angle
prop=join('SM_TransportPropeller',parts); prop.location=(4.6,-6,1.1)
export_objects=[body,ramp,prop]
for o in export_objects:
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active=o
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.uv.smart_project(island_margin=.025); bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.uv.export_layout(filepath=os.path.join(OUT,o.name+'_UV.svg'),mode='SVG',size=(2048,2048),opacity=.35)
    # Export explicit per-corner UV and normals to avoid importer axis/pivot ambiguity.
    me=o.data; me.calc_loop_triangles(); verts=[]; normals=[]; uvs=[]; tris=[]; mids=[]
    for tri in me.loop_triangles:
        ids=[]
        for li in tri.loops:
            co=me.vertices[me.loops[li].vertex_index].co; no=me.corner_normals[li].vector
            ids.append(len(verts)); verts.append([co.x*100,-co.y*100,co.z*100]); normals.append([no.x,-no.y,no.z]); uv=me.uv_layers.active.data[li].uv; uvs.append([uv.x,1-uv.y])
        tris.append(ids); mids.append(me.polygons[tri.polygon_index].material_index)
    with open(os.path.join(OUT,o.name+'.json'),'w') as f:json.dump(dict(vertices=verts,normals=normals,uvs=uvs,triangles=tris,materials=mids,slots=[m.name for m in me.materials]),f)
# Rigid ramp animation: mechanical panels should never bend under automatic weights.
ramp.rotation_euler.y=0; ramp.keyframe_insert('rotation_euler',frame=1)
ramp.rotation_euler.y=math.radians(-120); ramp.keyframe_insert('rotation_euler',frame=121)
scene=bpy.context.scene; scene.frame_end=121; scene.render.fps=30; scene.frame_set(1)
for y in [-12,6,12]:
    o=prop.copy(); o.data=prop.data; bpy.context.collection.objects.link(o); o.location=(4.6,y,1.1)
scene.world.color=(.18,.18,.18)
bpy.ops.object.camera_add(location=(35,-42,28)); camera=bpy.context.object; camera.rotation_euler=(Vector((0,0,1))-camera.location).to_track_quat('-Z','Y').to_euler(); camera.data.type='ORTHO'; camera.data.ortho_scale=50; scene.camera=camera
scene.render.engine='CYCLES'; scene.cycles.samples=8; scene.cycles.use_denoising=True; scene.display.shading.light='STUDIO'; scene.display.shading.color_type='MATERIAL'; scene.display.shading.show_shadows=True; scene.display.shading.show_cavity=True
bpy.ops.object.light_add(type='AREA',location=(5,-10,25)); bpy.context.object.data.energy=12000; bpy.context.object.data.shape='DISK'; bpy.context.object.data.size=20
scene.render.resolution_x=1200; scene.render.resolution_y=800; scene.render.resolution_percentage=100
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'CampusTransport.blend'))
scene.render.filepath=os.path.join(OUT,'Preview.png'); bpy.ops.render.render(write_still=True)
scene.frame_set(121); camera.location=(-24,-27,12); camera.rotation_euler=(Vector((-5,0,0))-camera.location).to_track_quat('-Z','Y').to_euler(); camera.data.ortho_scale=28; scene.render.filepath=os.path.join(OUT,'RampOpen.png'); bpy.ops.render.render(write_still=True)
print('TRANSPORT_BUILD_OK',sum(len(o.data.loop_triangles) for o in export_objects))

