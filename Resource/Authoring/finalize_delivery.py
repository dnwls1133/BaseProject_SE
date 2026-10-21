import sys,json,math
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
import build_resources as C
manifest=json.loads((C.ROOT/'Documentation/asset_manifest.json').read_text(encoding='utf-8'))
specs=[('human','Human_7H_v1','Characters/Human_Modular_Library.blend'),
       ('animal','Animal_v1','Creatures/Animal/Animal_Modular_Library.blend'),
       ('spider','Spider_v1','Creatures/Spider/Spider_Modular_Library.blend'),
       ('giant','Giant_v1','Creatures/Giant/Giant_Modular_Library.blend')]
profiles={}
for kind,rig,path in specs:
    walk,run=C.gait_speed(kind,'Walk'),C.gait_speed(kind,'Run')
    profiles[rig]={
        'walk_reference_speed_mps':walk,'run_reference_speed_mps':run,
        'speed_calibration':'Baked stance foot travel / stance duration; multiply by preset root_scale',
        'cycle_phase':{'start_frame':1,'walk_end_frame':32,'run_end_frame':24,
            'opposite_side_offset':.5,'stance_fraction_walk':.6,'stance_fraction_run':.4,
            'leg_pairing':'opposite left/right legs' if kind in ['human','giant'] else 'diagonal trot' if kind=='animal' else 'alternating tetrapod'},
        'states':{
            'Idle':{'clips':['Idle']},
            'Locomotion':{'clips':['Walk','Run'],'blend_parameter':'planar_speed_mps',
                'run_weight':'clamp((speed-walk_reference_speed)/(run_reference_speed-walk_reference_speed),0,1)',
                'sample_at_shared_normalized_phase':True},
            'Turn':{'clips':['TurnLeft90','TurnRight90'],'root_yaw_degrees':[90,-90]},
            'Attack':{'clips':['Attack'],'suggested_contact_frames':[18,22],'event_status':'suggested only'},
            'Jump':{'clips':['Jump'],'suggested_takeoff_frame':8,'suggested_landing_frame':30,
                    'root_translation':False,'visual_body_lift':True},
            'Hit':{'clips':['Hit']},'Death':{'clips':['Death'],'hold_last_frame':True}},
        'transitions_seconds':{'Idle_to_Locomotion':.15,'Locomotion_to_Idle':.20,
            'Walk_to_Run':.18,'Run_to_Walk':.22,'Turn_entry':.12,'Turn_exit':.15,
            'Attack_entry':.10,'Attack_exit':.18,'Hit_entry':.08,'Hit_exit':.15},
        'authored_transition_clips':{
            'WalkToRun':{'frames':[1,18],'entry_normalized_phase':0,'exit_normalized_phase':0},
            'RunToWalk':{'frames':[1,20],'entry_normalized_phase':0,'exit_normalized_phase':0}},
        'root_motion_policy':{'planar_translation':'in-place','turn_yaw':'consume once or strip before entity yaw',
            'jump':'body bone visual lift; runtime physics displacement must avoid double lift'},
        'rotation_interpolation':'normalized quaternion shortest-path SLERP',
        'reference_pose':'BindPose_A','neutral_transition_pose':'Idle'
    }
    for clip in ['Walk','Run']:
        manifest['animations'][rig][clip]['nominal_speed_mps']=C.gait_speed(kind,clip)
    C.bpy.ops.wm.open_mainfile(filepath=str(C.ROOT/path))
    for clip in ['Walk','Run']:
        C.bpy.data.actions[clip]['nominal_speed_mps']=C.gait_speed(kind,clip)
    C.bpy.context.preferences.filepaths.save_version=0
    # Ready-to-view native file: material colors, selection framed, no UI interaction required.
    for screen in C.bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.shading.type='MATERIAL'
                area.spaces.active.region_3d.view_distance=4.5 if kind=='human' else 7
                area.spaces.active.region_3d.view_location=(0,0,1 if kind=='human' else 1.5)
    C.save_blend(C.ROOT/path)

blending={'schema':'resource.animation_blending','version':1,'fps':30,
    'status':'Authored asset combination metadata; no game state-machine implementation',
    'profiles':profiles,
    'preset_scales':{a['id']:a.get('root_scale',1) for a in manifest['assets'] if a['category'] in ['character_preset','creature']}}
(C.ROOT/'Documentation/animation_blending.json').write_text(json.dumps(blending,ensure_ascii=False,indent=2),encoding='utf-8')
(C.ROOT/'Documentation/asset_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')

# Dedicated inventory previews with objects at their export-local origin, independent of gallery positions.
C.bpy.ops.wm.open_mainfile(filepath=str(C.ROOT/'Environment/Environment_Modular_Library.blend'))
entries=manifest['assets']
objects={o.name:o for o in C.bpy.context.scene.objects}
for cat,spacing,columns,scale in [('architecture',3.2,5,19),('prop',1.6,6,11),('nature',2.6,6,19)]:
    for o in C.bpy.context.scene.objects:
        if o.type=='MESH':
            o.hide_render=True
    selected=[r for r in entries if r['category']==cat]
    stage=C.collection('Inventory_'+cat)
    for i,rec in enumerate(selected):
        ob=objects[rec['id']]
        ob.hide_render=False
        root=ob.parent if ob.parent else ob
        root.location=((i%columns-(columns-1)/2)*spacing,(i//columns)*spacing,0)
        text=C.bpy.data.curves.new('AssetLabel','FONT')
        text.body=rec['id']
        text.align_x='CENTER'
        text.size=.105 if cat=='prop' else .13
        label=C.bpy.data.objects.new('AssetLabel',text)
        stage.objects.link(label)
        label.location=(root.location.x,root.location.y-.65,.005)
    C.bpy.context.scene.render.resolution_x=1800
    C.bpy.context.scene.render.resolution_y=1150
    rows=math.ceil(len(selected)/columns)
    center=(rows-1)*spacing/2
    cam=C.stage_camera('Inventory_Camera',(8,-13+center,15),(0,center,.6),scale)
    C.bpy.context.view_layer.update()
    inverse=cam.matrix_world.inverted()
    points=[]
    candidates=[objects[r['id']] for r in selected]+list(stage.objects)
    for ob in candidates:
        for corner in ob.bound_box:
            points.append(inverse @ (ob.matrix_world @ Vector(corner)))
    low_x,high_x=min(p.x for p in points),max(p.x for p in points)
    low_y,high_y=min(p.y for p in points),max(p.y for p in points)
    aspect=C.bpy.context.scene.render.resolution_x/C.bpy.context.scene.render.resolution_y
    cam.data.ortho_scale=max(high_x-low_x,(high_y-low_y)*aspect)*1.14
    cam.location+=cam.matrix_world.to_quaternion() @ Vector(((low_x+high_x)/2,(low_y+high_y)/2,0))
    C.render(C.ROOT/'Previews'/('05_'+cat.title()+'_Inventory.png'))
    for o in list(stage.objects):
        C.bpy.data.objects.remove(o,do_unlink=True)
print('FINAL_DELIVERY_READY',flush=True)
