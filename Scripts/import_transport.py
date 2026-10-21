"""Run with UnrealEditor-Cmd -run=pythonscript -script=... -nullrhi. Never overwrites existing assets."""
import unreal as u, json, os
SRC=os.environ.get('TRANSPORT_OUTPUT',os.path.join(os.path.dirname(os.path.dirname(__file__)),'ArtSource','Transport'))
ROOT='/Game/CampusVehicles/Transport'
at=u.AssetToolsHelpers.get_asset_tools(); lib=u.EditorAssetLibrary; mel=u.MaterialEditingLibrary
task=u.AssetImportTask();task.filename=os.path.join(SRC,'T_Transport_Panels.png');task.destination_path=ROOT;task.destination_name='T_Transport_Panels';task.automated=True;task.save=True
at.import_asset_tasks([task])
panel_texture=lib.load_asset(ROOT+'/T_Transport_Panels')
if not panel_texture:raise RuntimeError('Panel texture import failed')
panel_texture.set_editor_property('max_texture_size',1024);lib.save_loaded_asset(panel_texture)
def material(name,color):
    path=ROOT+'/'+name
    if lib.does_asset_exist(path):return lib.load_asset(path)
    m=at.create_asset(name,ROOT,u.Material,u.MaterialFactoryNew())
    tex=mel.create_material_expression(m,u.MaterialExpressionTextureSampleParameter2D,-600,0)
    tex.set_editor_property('parameter_name','BaseColorTexture'); tex.set_editor_property('texture',panel_texture if name=='M_TransportExterior' else lib.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    tint=mel.create_material_expression(m,u.MaterialExpressionVectorParameter,-600,200)
    tint.set_editor_property('parameter_name','Tint'); tint.set_editor_property('default_value',u.LinearColor(*color,1))
    mul=mel.create_material_expression(m,u.MaterialExpressionMultiply,-250,0)
    mel.connect_material_expressions(tex,'RGB',mul,'A');mel.connect_material_expressions(tint,'',mul,'B');mel.connect_material_property(mul,'',u.MaterialProperty.MP_BASE_COLOR)
    for param,val,output in [('Roughness',.65,u.MaterialProperty.MP_ROUGHNESS),('Metallic',.25,u.MaterialProperty.MP_METALLIC)]:
        n=mel.create_material_expression(m,u.MaterialExpressionScalarParameter,-250,250)
        n.set_editor_property('parameter_name',param); n.set_editor_property('default_value',val);mel.connect_material_property(n,'',output)
    mel.recompile_material(m);lib.save_loaded_asset(m);return m
mats={'Exterior_Replaceable':material('M_TransportExterior',(.32,.38,.29)), 'Mechanical':material('M_TransportMechanical',(.035,.05,.06)), 'Interior':material('M_TransportInterior',(.24,.27,.25))}
for name,tint in [('MI_Transport_Default',(.32,.38,.29)),('MI_Transport_Winter',(.7,.78,.85)),('MI_Transport_Festival',(.6,.12,.035))]:
    if not lib.does_asset_exist(ROOT+'/'+name):
        mi=at.create_asset(name,ROOT,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew());mel.set_material_instance_parent(mi,mats['Exterior_Replaceable']);mel.set_material_instance_vector_parameter_value(mi,'Tint',u.LinearColor(*tint,1));lib.save_loaded_asset(mi)
default=lib.load_asset(ROOT+'/MI_Transport_Default')
report={}
for name in ['SM_TransportBody','SM_TransportRamp','SM_TransportPropeller']:
    data=json.load(open(os.path.join(SRC,name+'.json')))
    if lib.does_asset_exist(ROOT+'/'+name):raise RuntimeError('Refusing to overwrite '+name+'; back up/move existing transport assets first')
    dm=u.DynamicMesh();u.GeometryScript_Materials.enable_material_i_ds(dm)
    for mid in range(len(data['slots'])):
        b=u.GeometryScriptSimpleMeshBuffers();b.vertices=[u.Vector(*v) for v in data['vertices']];b.normals=[u.Vector(*v) for v in data['normals']];b.uv0=[u.Vector2D(*v) for v in data['uvs']]
        b.triangles=[u.IntVector(*t) for t,m in zip(data['triangles'],data['materials']) if m==mid]
        u.GeometryScript_MeshEdits.append_buffers_to_mesh(dm,b,mid)
    options=u.GeometryScriptCreateNewStaticMeshAssetOptions();options.enable_collision=False;options.enable_nanite=False;options.enable_recompute_normals=False;options.enable_recompute_tangents=True
    mesh,result=u.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,ROOT+'/'+name,options)
    if not mesh:raise RuntimeError(str(result))
    mesh.set_editor_property('static_materials',[u.StaticMaterial(material_interface=default if s=='Exterior_Replaceable' else mats[s],material_slot_name=s) for s in data['slots']])
    # Commandlets lack the StaticMeshEditorSubsystem; finalize LODs in verify_transport_editor.py.
    lib.save_loaded_asset(mesh)
    report[name]={'triangles':len(data['triangles']),'slots':data['slots'],'lod_setup':'Run verify_transport_editor.py in the editor'}
lib.save_directory(ROOT)
json.dump(report,open(os.path.join(SRC,'UnrealImportReport.json'),'w'),indent=2)
u.log('TRANSPORT_IMPORT_OK '+str(report))
