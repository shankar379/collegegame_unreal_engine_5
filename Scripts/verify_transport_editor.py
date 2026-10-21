"""Run in a full editor session on /Engine/Maps/Entry with -ExecutePythonScript (not -run=pythonscript)."""
import unreal as u, json, os
ROOT='/Game/CampusVehicles/Transport'
OUT=os.path.join(u.Paths.project_dir(),'ArtSource','Transport')
lib=u.EditorAssetLibrary
sms=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
assert sms, 'StaticMeshEditorSubsystem requires a full editor session'
report={}
for name in ['SM_TransportBody','SM_TransportRamp','SM_TransportPropeller']:
    mesh=lib.load_asset(ROOT+'/'+name);assert mesh,name
    reduction=u.StaticMeshReductionOptions();reduction.auto_compute_lod_screen_size=False
    reduction.reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=1,screen_size=1),u.StaticMeshReductionSettings(percent_triangles=.5,screen_size=.35),u.StaticMeshReductionSettings(percent_triangles=.2,screen_size=.12)]
    result=sms.set_lods(mesh,reduction)
    count=sms.get_lod_count(mesh);assert count==3,(name,result,count)
    lib.save_loaded_asset(mesh)
    box=mesh.get_bounding_box()
    report[name]={'lods':count,'triangles':[mesh.get_num_triangles(i) for i in range(count)],'slots':[str(s.material_slot_name) for s in mesh.get_editor_property('static_materials')],'bounds_cm':[[box.min.x,box.min.y,box.min.z],[box.max.x,box.max.y,box.max.z]]}
cls=u.load_class(None,'/Script/CollegeCampusGame.CampusBRPlane');assert cls
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
plane=actors.spawn_actor_from_class(cls,u.Vector(0,0,1000));assert plane
components=plane.get_components_by_class(u.StaticMeshComponent)
assert len(components)==6,len(components)
assert all(c.static_mesh for c in components)
skin=lib.load_asset(ROOT+'/MI_Transport_Winter');plane.set_event_livery(skin)
for c in components:
    if str(c.get_name()) in ['Fuselage','RearRamp']:
        index=c.get_material_index('Exterior_Replaceable');assert index>=0
        assert c.get_material(index)==skin,str(c.get_name())
report['actor']={'mesh_components':len(components),'event_material_switch':'passed','physics_simulation':any(c.is_simulating_physics() for c in components)}
actors.destroy_actor(plane)
json.dump(report,open(os.path.join(OUT,'VerifiedReport.json'),'w'),indent=2)
u.log('TRANSPORT_VERIFIED '+str(report))
u.SystemLibrary.quit_editor()
