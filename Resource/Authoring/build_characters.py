import math
from mathutils import Vector

def build(C):
    C.new_scene('Human_Modular_Library')
    col=C.collection('HumanKit_Parts')
    bones=[('root',(0,0,0),(0,0,.15),None),
        ('pelvis',(0,0,.93),(0,0,1.05),'root'),
        ('spine',(0,0,1.05),(0,0,1.25),'pelvis'),
        ('chest',(0,0,1.25),(0,0,1.46),'spine'),
        ('neck',(0,0,1.46),(0,0,1.56),'chest'),
        ('head',(0,0,1.56),(0,0,1.80),'neck'),
        ('cape',(0,.13,1.35),(0,.19,.65),'chest')]
    for side,s in [('L',1),('R',-1)]:
        bones.extend([
            ('clavicle.'+side,(0,0,1.44),(s*.235,0,1.44),'chest'),
            ('upper_arm.'+side,(s*.235,0,1.44),(s*.435,0,1.195),'clavicle.'+side),
            ('forearm.'+side,(s*.435,0,1.195),(s*.615,-.006,.985),'upper_arm.'+side),
            ('hand.'+side,(s*.615,-.006,.985),(s*.69,-.01,.90),'forearm.'+side),
            ('thigh.'+side,(s*.108,0,.95),(s*.135,-.018,.51),'pelvis'),
            ('shin.'+side,(s*.135,-.018,.51),(s*.145,0,.105),'thigh.'+side),
            ('foot.'+side,(s*.145,0,.105),(s*.145,-.17,.055),'shin.'+side),
            ('socket.hand.'+side,(s*.65,-.01,.95),(s*.65,-.10,.95),'hand.'+side)])
        for i in range(5):
            bones.append((f'finger{i+1}.{side}',(s*(.64+i*.013),-.01,.944),
                          (s*(.68+i*.012),-.012,.89),'hand.'+side))
    bones.extend([('socket.back',(0,.12,1.32),(0,.2,1.32),'chest'),
                  ('socket.belt',(0,-.12,1.01),(0,-.2,1.01),'pelvis'),
                  ('socket.head',(0,0,1.77),(0,0,1.86),'head')])
    arm=C.rig('Human_7H_v1',bones,col)
    parts={}
    def part(name,slot,m):
        o=m.finish(name,col,arm,slot)
        o['body_height_m']=1.82
        o['head_unit_m']=.26
        o['seam_standard']='Human7H_v1'
        parts[name]=o
        return o

    for n,skin,wide in [('Head_Ashen','AshSkin',1),('Head_Pale','PaleSkin',.97),
                         ('Head_Umber','DarkSkin',1.03)]:
        m=C.Mesh()
        m.loft([(0,0,1.56,.051,.054),(0,-.013,1.60,.067*wide,.074),
            (0,0,1.66,.091*wide,.091),(0,.003,1.74,.092*wide,.092),
            (0,.008,1.795,.063*wide,.066),(0,.008,1.82,.012,.018)],skin,'head')
        for s in [-1,1]:
            m.ell((s*.092,.003,1.69),(.016,.019,.034),skin,'head',8,4)
        part(n,'head',m)
    for n,skin,age,scar in [('Face_Quiet','AshSkin',0,False),
                            ('Face_Weathered','PaleSkin',1,True),
                            ('Face_Umber','DarkSkin',.5,False)]:
        m=C.Mesh()
        for s in [-1,1]:
            m.ell((s*.037,-.084,1.704),(.024,.009,.012),'Black','head',8,4)
            m.ell((s*.037,-.092,1.705),(.014,.004,.006),'PaleSkin','head',8,3)
            m.ell((s*.036,-.096,1.705),(.005,.003,.006),'Black','head',6,3)
            m.beam((s*.015,-.09,1.725),(s*.061,-.076,1.724-age*.005),.012,.010,
                   'WornLeather','head')
            m.ell((s*.051,-.070,1.67),(.025,.015,.019),skin,'head',8,3)
            if age:
                m.beam((s*.028,-.086,1.691),(s*.059,-.075,1.687),.003,.003,'WornLeather','head')
        m.ell((0,-.087,1.682),(.015,.024,.034),skin,'head',8,4)
        m.beam((-.026,-.078,1.636),(.026,-.078,1.636),.005,.005,'WornLeather','head')
        if scar:
            m.beam((-.052,-.086,1.741),(-.035,-.091,1.665),.004,.003,'BloodCloth','head')
        part(n,'face',m)
    m=C.Mesh()
    for i in range(11):
        a=math.pi*2*i/11
        m.tube([(math.cos(a)*.07,math.sin(a)*.065,1.79),
                (math.cos(a)*.095,math.sin(a)*.088,1.75),
                (math.cos(a)*.093,math.sin(a)*.095,1.71 if math.sin(a)>-.4 else 1.75)],
                [.029,.024,.007],'CharcoalCloth','head',6)
    part('Hair_Cropped','hair',m)
    m=C.Mesh()
    for i in range(9):
        a=i*math.pi/8
        m.tube([(.09*math.cos(a),.01+.065*math.sin(a),1.79),
                (.105*math.cos(a),.01+.08*math.sin(a),1.66),
                (.085*math.cos(a),.04+.09*math.sin(a),1.55)],
                [.027,.025,.009],'WornLeather','head',6)
    part('Hair_Long','hair',m)
    m=C.Mesh()
    # Open hood: back and sides only, leaving the face readable.
    vs=[]
    for z,r in [(1.53,.109),(1.66,.123),(1.79,.117),(1.855,.05)]:
        for j in range(9):
            a=-.24+math.pi*1.15*j/8
            vs.append((r*math.cos(a),r*math.sin(a)+.005,z))
    fs=[]
    for i in range(3):
        for j in range(8):
            a=i*9+j
            fs.append((a,a+1,a+10,a+9))
    m.add(vs,fs,'CharcoalCloth','head')
    for j in [0,8]:
        m.tube([vs[i*9+j] for i in range(4)],[.009]*4,'FadedLinen','head',6)
    part('Hood_Pilgrim','headwear',m)
    m=C.Mesh()
    m.loft([(0,.004,1.72,.105,.10),(0,.004,1.79,.103,.10),
            (0,.004,1.835,.07,.073),(0,.004,1.85,.01,.02)],'RustedIron','head')
    m.beam((0,-.102,1.73),(0,-.102,1.61),.014,.014,'RustedIron','head')
    for s in [-1,1]:
        m.ell((s*.09,.005,1.66),(.022,.06,.074),'RustedIron','head',8,4)
    part('Helmet_Warden','headwear',m)

    for n,mat,bulk,detail in [('Torso_Bare','AshSkin',0,'skin'),
        ('Torso_Linen','FadedLinen',.012,'cloth'),
        ('Torso_Leather','WornLeather',.022,'leather'),
        ('Torso_Iron','RustedIron',.035,'iron')]:
        m=C.Mesh()
        rings=[(0,0,.98,.132+bulk,.083+bulk),(0,0,1.07,.124+bulk,.078+bulk),
               (0,0,1.21,.157+bulk,.095+bulk),(0,0,1.36,.214+bulk,.107+bulk),
               (0,0,1.44,.222+bulk,.095+bulk),(0,0,1.485,.090,.067)]
        weights=['pelvis',{'pelvis':.6,'spine':.4},'spine',{'spine':.2,'chest':.8},'chest','chest']
        m.loft(rings,mat,weights)
        m.loft([(0,0,1.47,.061,.057),(0,0,1.56,.051,.054)],'AshSkin','neck',10)
        if detail!='skin':
            m.loft([(0,0,1.055,.14+bulk,.095+bulk),(0,0,1.09,.14+bulk,.095+bulk)],
                   'WornLeather',{'pelvis':.5,'spine':.5})
            m.box((0,-.108-bulk,1.074),(.056,.018,.045),'TarnishedBrass','spine')
            for i in range(6):
                z=1.12+i*.045
                m.beam((-.024,-.106-bulk,z),(.024,-.106-bulk,z+.01),.008,.008,
                       'OldBone','spine' if z<1.26 else 'chest')
        if detail=='leather':
            m.tube([(-.17,-.1,1.42),(-.07,-.12,1.26),(.10,-.105,1.10)],
                   [(.028,.007)]*3,'CharcoalCloth',['chest','spine','spine'],6)
        if detail=='iron':
            for s in [-1,1]:
                m.ell((s*.101,-.096,1.36),(.11,.042,.083),'RustedIron','chest',10,4)
            for z in [1.13,1.19,1.25]:
                m.box((0,-.115,z),(.22,.032,.042),'RustedIron','spine')
        part(n,'torso',m)
    for n,mat,bulk in [('Arms_Bare','AshSkin',0),('Arms_Linen','FadedLinen',.014),
                         ('Arms_Leather','WornLeather',.025),('Arms_Iron','RustedIron',.027)]:
        m=C.Mesh()
        for side,s in [('L',1),('R',-1)]:
            up,lo='upper_arm.'+side,'forearm.'+side
            pts=[(s*.225,0,1.435),(s*.31,0,1.34),(s*.435,0,1.195),
                 (s*.51,-.003,1.11),(s*.615,-.006,.985)]
            m.tube(pts,[.075+bulk,.068+bulk,.052+bulk,.054+bulk,.036+bulk*.3],mat,
                   [up,up,{up:.5,lo:.5},lo,lo],10)
            if n!='Arms_Bare':
                m.tube([(s*.56,-.004,1.05),(s*.59,-.005,1.015)],[.052,.048],
                       'WornLeather',lo,8)
            if n=='Arms_Iron':
                m.ell((s*.265,0,1.43),(.115,.12,.074),'RustedIron',up,10,4)
                m.ell((s*.46,-.022,1.178),(.068,.074,.055),'RustedIron',lo,8,4)
        part(n,'arms',m)
    for n,mat in [('Hands_Bare','AshSkin'),('Hands_Gloves','WornLeather')]:
        m=C.Mesh()
        for side,s in [('L',1),('R',-1)]:
            m.ell((s*.645,-.009,.949),(.048,.025,.053),mat,'hand.'+side,8,4)
            for i in range(4):
                m.tube([(s*(.646+i*.015),-.012,.937),(s*(.68+i*.012),-.014,.903),
                        (s*(.69+i*.012),-.027,.889)], [.008,.007,.004],mat,f'finger{i+1}.{side}',5)
            m.tube([(s*.62,-.018,.952),(s*.62,-.042,.922),(s*.65,-.049,.91)],
                   [.012,.009,.005],mat,'finger5.'+side,5)
        part(n,'hands',m)
    for n,mat,bulk in [('Legs_Bare','AshSkin',0),('Legs_Linen','FadedLinen',.012),
                         ('Legs_Leather','WornLeather',.02)]:
        m=C.Mesh()
        m.loft([(0,0,.88,.14+bulk,.085+bulk),(0,0,.98,.14+bulk,.085+bulk)],mat,'pelvis')
        for side,s in [('L',1),('R',-1)]:
            up,lo='thigh.'+side,'shin.'+side
            m.tube([(s*.105,0,.95),(s*.12,0,.76),(s*.135,-.018,.51),
                    (s*.14,0,.33),(s*.145,0,.12)],
                   [.094+bulk,.084+bulk,.056+bulk,.065+bulk,.036],mat,
                   [up,up,{up:.5,lo:.5},lo,lo],10)
        part(n,'legs',m)
    for n,mat,high in [('Feet_Bare','AshSkin',False),('Boots_Leather','WornLeather',True),
                         ('Boots_Iron','RustedIron',True)]:
        m=C.Mesh()
        for side,s in [('L',1),('R',-1)]:
            m.ell((s*.145,-.065,.065),(.057,.128,.065),mat,'foot.'+side,10,4)
            if high:
                m.tube([(s*.145,0,.10),(s*.142,0,.28),(s*.14,0,.37)],
                    [.052,.071,.066],mat,['shin.'+side]*3,10)
                for z in [.19,.30]:
                    m.box((s*.145,-.062,z),(.105,.017,.025),'CharcoalCloth','shin.'+side)
                    m.box((s*.17,-.075,z),(.025,.009,.026),'TarnishedBrass','shin.'+side)
        part(n,'feet',m)
    for n,mat,length in [('Skirt_Pilgrim','CharcoalCloth',.60),
                           ('Skirt_Healer','FadedLinen',.47),('Tabard_Warden','BloodCloth',.63)]:
        m=C.Mesh()
        # Split panels allow independent leg silhouettes; no solid cone across the stride.
        for side,s in [('L',1),('R',-1)]:
            v=[]
            for z,width,y in [(1.05,.135,.115),(.81,.16,.13),(length,.18,.16)]:
                for x in [s*.012,s*width]:
                    v.append((x,-y,z))
                    v.append((x,y,z))
            m.add(v,[(0,4,6,2),(4,8,10,6),(1,3,7,5),(5,7,11,9),
                     (2,6,7,3),(6,10,11,7)],mat,
                  [{'pelvis':1}]*4+[{'pelvis':.45,'thigh.'+side:.55}]*4+['unused']*0+
                  [{'thigh.'+side:1}]*4)
            for y in [-1,1]:
                m.beam((s*.013,y*.116,1.04),(s*.014,y*.16,length),.008,.008,
                       'FadedLinen' if mat!='FadedLinen' else 'CharcoalCloth','thigh.'+side)
        part(n,'skirt',m)
    for n,mat in [('Cape_Pilgrim','CharcoalCloth'),('Cape_Healer','BloodCloth')]:
        m=C.Mesh()
        v=[]
        weights=[]
        for i,(z,w,y) in enumerate([(1.46,.23,.11),(1.28,.25,.145),(.95,.30,.20),(.56,.33,.26)]):
            for j in range(7):
                x=-w+2*w*j/6
                v.append((x,y+.02*math.cos(j*math.pi),z-(.025*(j%2) if i==3 else 0)))
                weights.append({'chest':1} if i==0 else {'cape':1} if i>1 else {'chest':.4,'cape':.6})
        f=[]
        for i in range(3):
            for j in range(6):
                a=i*7+j
                f.extend([(a,a+7,a+8,a+1),(a+1,a+8,a+7,a)])
        m.add(v,f,mat,weights)
        part(n,'cape',m)
    m=C.Mesh()
    m.tube([(-.18,-.10,1.43),(-.06,-.14,1.27),(.13,-.12,1.06)],
           [(.025,.009)]*3,'WornLeather',['chest','spine','spine'],6)
    m.ell((.17,-.015,1.02),(.07,.055,.075),'WornLeather','pelvis',8,4)
    m.box((.17,-.061,1.055),(.09,.012,.035),'DeadWood','pelvis')
    m.ell((-.17,-.018,.99),(.026,.026,.058),'Amber','pelvis',8,4)
    m.tube([(-.17,-.018,1.035),(-.17,-.018,1.055)],[.014,.014],'TarnishedBrass','pelvis',8)
    part('Gear_PouchAndBloodVial','gear',m)
    m=C.Mesh()
    for s in [-1,1]:
        m.ell((s*.23,-.015,1.43),(.112,.126,.068),'RustedIron','upper_arm.'+('L' if s>0 else 'R'),10,4)
        for z in [1.42,1.45]:
            m.ell((s*.28,-.085,z),(.011,.009,.011),'TarnishedBrass','upper_arm.'+('L' if s>0 else 'R'),6,3)
    part('Shoulders_Iron','shoulders',m)
    m=C.Mesh()
    m.ell((0,.18,1.26),(.15,.074,.19),'WornLeather','chest',10,4)
    for x in [-.09,.09]:
        m.tube([(x,.04,1.43),(x,.23,1.4),(x,.25,1.12)], [.015]*3,'FadedLinen','chest',6)
    m.tube([(-.16,.2,1.47),(.16,.2,1.47)],[.045,.045],'CharcoalCloth','chest',8)
    part('Gear_ScavengerPack','back',m)
    m=C.Mesh()
    # Weapon points down in the A-pose and is deformed by the common hand bone.
    m.tube([(-.68,-.035,.92),(-.76,-.035,.82)],[.018,.017],'WornLeather','hand.R',8)
    m.beam((-.80,-.035,.89),(-.66,-.035,.79),.021,.018,'RustedIron','hand.R')
    m.tube([(-.76,-.035,.82),(-1.11,-.035,.36),(-1.13,-.035,.32)],
           [(.035,.008),(.024,.006),(.001,.001)],'RustedIron','hand.R',4)
    part('Weapon_PilgrimSword','weapon',m)
    m=C.Mesh()
    m.tube([(-.69,-.035,.97),(-.84,-.035,.77),(-1.02,-.035,.51)],
           [.025,.023,.018],'DeadWood','hand.R',8)
    m.box((-1.01,-.035,.58),(.18,.065,.13),'RustedIron','hand.R')
    part('Weapon_WorkHammer','weapon',m)
    m=C.Mesh()
    for s in [-1,1]:
        m.tube([(s*.09,.02,1.78),(s*.13,.04,1.91),(s*.11,.05,1.98)],
               [.025,.018,.001],'OldBone','head',7)
    m.ell((-.06,-.075,1.67),(.025,.022,.04),'InfectedFlesh','head',8,4)
    part('Mutation_HornAndScab','mutation',m)

    presets={
        'Pilgrim':['Head_Ashen','Face_Quiet','Hood_Pilgrim','Torso_Leather','Arms_Linen',
                   'Hands_Gloves','Legs_Leather','Boots_Leather','Skirt_Pilgrim','Cape_Pilgrim',
                   'Gear_PouchAndBloodVial','Weapon_PilgrimSword'],
        'Healer':['Head_Pale','Face_Weathered','Hair_Long','Torso_Linen','Arms_Linen',
                  'Hands_Bare','Legs_Linen','Boots_Leather','Skirt_Healer','Cape_Healer','Gear_PouchAndBloodVial'],
        'Warden':['Head_Ashen','Face_Quiet','Helmet_Warden','Torso_Iron','Arms_Iron',
                  'Hands_Gloves','Legs_Leather','Boots_Iron','Tabard_Warden','Weapon_PilgrimSword'],
        'Scavenger':['Head_Umber','Face_Umber','Hair_Cropped','Torso_Leather','Arms_Leather',
                     'Hands_Gloves','Legs_Linen','Boots_Leather','Gear_ScavengerPack','Weapon_WorkHammer'],
        'InfectedWanderer':['Head_Ashen','Face_Quiet','Torso_Linen','Arms_Bare','Hands_Bare',
                            'Legs_Linen','Feet_Bare','Mutation_HornAndScab'],
        'Body_7H':['Head_Ashen','Face_Quiet','Torso_Bare','Arms_Bare','Hands_Bare','Legs_Bare','Feet_Bare']}
    C.animate(arm,'human')
    for n,o in parts.items():
        p=C.ROOT/'Characters/Modules'/(n+'.glb')
        C.export_glb(p,[arm,o])
        C.asset_record(n,[o],p,'human_module',slot=o['module_slot'],rig=arm['rig_id'])
    for name,names in presets.items():
        objects=[parts[n] for n in names]
        p=C.ROOT/'Characters/Presets'/(name+'.glb')
        C.export_glb(p,[arm]+objects,True)
        C.asset_record(name,objects,p,'character_preset',modules=names,rig=arm['rig_id'])
    # Set an idle action active for a ready-to-scrub native file, retaining every clip in NLA.
    arm.animation_data.action=next(a for a in C.bpy.data.actions if a.name=='Idle')
    visible=presets['Pilgrim']
    for name,o in parts.items():
        o.hide_set(name not in visible)
        o.hide_render=name not in visible
    C.select_only([arm]+[parts[n] for n in visible])
    C.bpy.context.scene.frame_end=60
    C.bpy.context.scene.frame_set(1)
    C.stage_camera('Human_Review',(3,-6,2.9),(0,0,.95),2.5)
    C.render_setup()
    C.save_blend(C.ROOT/'Characters/Human_Modular_Library.blend')
    # Review lineup, separate rigs so parts remain bound to their own skeleton.
    for o in parts.values():
        o.hide_render=True
        o.hide_set(True)
    stage=C.collection('Review_Lineup')
    for i,(name,names) in enumerate(presets.items()):
        if name=='Body_7H':
            continue
        a=arm.copy()
        a.data=arm.data.copy()
        a.animation_data_clear()
        stage.objects.link(a)
        a.location.x=(i-2)*1.45
        a.rotation_euler.z=-.12
        for n in names:
            ob=parts[n].copy()
            stage.objects.link(ob)
            ob.parent=a
            ob.modifiers['Skin'].object=a
            ob.hide_render=False
            ob.hide_set(False)
    C.floor_mesh(stage)
    C.stage_camera('Lineup_Review',(4,-11,5),(0,0,1),8.9)
    C.render(C.ROOT/'Previews/01_Characters.png')
    C.save_blend(C.ROOT/'Characters/Character_Lineup.blend')
    (C.ROOT/'Documentation/human_presets.json').write_text(C.json.dumps(presets,indent=2),encoding='utf-8')
    print('CHARACTERS_COMPLETE',len(parts),flush=True)
