import sys, json
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
import build_resources as C
manifest=json.loads((C.ROOT/'Documentation/asset_manifest.json').read_text(encoding='utf-8'))
human_presets=json.loads((C.ROOT/'Documentation/human_presets.json').read_text(encoding='utf-8'))
specs=[('human','Characters/Human_Modular_Library.blend','Human_7H_v1'),
       ('animal','Creatures/Animal/Animal_Modular_Library.blend','Animal_v1'),
       ('spider','Creatures/Spider/Spider_Modular_Library.blend','Spider_v1'),
       ('giant','Creatures/Giant/Giant_Modular_Library.blend','Giant_v1')]

for kind,file,rig_id in specs:
    C.bpy.ops.wm.open_mainfile(filepath=str(C.ROOT/file))
    arm=next(o for o in C.bpy.context.scene.objects if o.type=='ARMATURE' and o.get('rig_id')==rig_id)
    hidden={o.name:(o.hide_get(),o.hide_render) for o in C.bpy.context.scene.objects}
    arm.animation_data_clear()
    for a in list(C.bpy.data.actions):
        C.bpy.data.actions.remove(a)
    C.animate(arm,kind)
    actions={a.name:a for a in C.bpy.data.actions}
    presets=[a for a in manifest['assets'] if a['category'] in ['character_preset','creature'] and a['rig']==rig_id]
    for rec in presets:
        objects=[C.bpy.data.objects[n if kind=='human' else kind.title()+'_'+n] for n in rec['modules']]
        arm.scale=(rec.get('root_scale',1),)*3
        C.export_glb(C.ROOT/rec['file'],[arm]+objects,True)
    arm.scale=(1,1,1)
    for n,(view,render) in hidden.items():
        ob=C.bpy.data.objects.get(n)
        if ob:
            ob.hide_set(view)
            ob.hide_render=render
    arm.animation_data.action=actions['Idle']
    C.bpy.context.scene.frame_set(1)
    C.save_blend(C.ROOT/file)
    # Contact sheet of actual skinned geometry evaluated at the given action frames.
    default=human_presets['Pilgrim'] if kind=='human' else next(a['modules'] for a in presets if a['tier']=='danger')
    visible=[C.bpy.data.objects[n if kind=='human' else kind.title()+'_'+n] for n in default]
    for o in C.bpy.context.scene.objects:
        if o.type=='MESH':
            o.hide_render=True
    for o in visible:
        o.hide_set(False)
    stage=C.collection('Animation_ContactSheet')
    samples=[('BindPose_A',1),('Walk',9),('Run',7),('TurnLeft90',12),('Attack',19),('Jump',18)]
    size=.65 if kind=='giant' else 1
    spacing=2.5 if kind!='spider' else 3.1
    for i,(clip,frame) in enumerate(samples):
        arm.animation_data.action=actions[clip]
        C.bpy.context.scene.frame_set(frame)
        C.bpy.context.view_layer.update()
        deps=C.bpy.context.evaluated_depsgraph_get()
        for original in visible:
            evaluated=original.evaluated_get(deps)
            mesh=C.bpy.data.meshes.new_from_object(evaluated,preserve_all_data_layers=True,depsgraph=deps)
            o=C.bpy.data.objects.new(f'{clip}_{original.name}',mesh)
            stage.objects.link(o)
            o.location=((i-2.5)*spacing,0,0)
            o.scale=(size,)*3
        font=C.bpy.data.curves.new('PoseLabel','FONT')
        font.body=clip
        font.size=.15
        font.align_x='CENTER'
        label=C.bpy.data.objects.new('PoseLabel_'+clip,font)
        stage.objects.link(label)
        label.location=((i-2.5)*spacing,-.3,-.01)
    C.MATS={n:C.bpy.data.materials[n] for n in C.PALETTE if n in C.bpy.data.materials}
    C.floor_mesh(stage)
    C.stage_camera('Motion_Review',(4,-15,8),(0,0,.8),spacing*6.6)
    scene=C.bpy.context.scene
    scene.render.resolution_x=1900
    scene.render.resolution_y=700
    scene.cycles.samples=24
    C.render(C.ROOT/'Previews'/('04_'+kind.title()+'_Motion.png'))
    print('ANIMATION_POLISH_COMPLETE',kind,flush=True)
manifest['animations'].update(C.MANIFEST['animations'])
(C.ROOT/'Documentation/asset_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
