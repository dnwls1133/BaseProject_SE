import math
from mathutils import Vector

def build(C):
    all_reviews=[]
    for family in ['animal','spider','giant']:
        C.new_scene(family.title()+'_Modular_Library')
        col=C.collection(family.title()+'_Parts')
        bones=[('root',(0,0,0),(0,0,.15),None)]
        endpoints={}
        if family=='animal':
            bones += [('body',(0,.40,.96),(0,-.25,1.1),'root'),
                      ('head',(0,-.36,1.12),(0,-.95,1.15),'body'),
                      ('jaw',(0,-.67,1.04),(0,-1.12,1.02),'head'),
                      ('tail',(0,.55,.94),(0,1.12,.70),'body'),
                      ('tail.tip',(0,1.12,.70),(0,1.55,.61),'tail')]
            for side,s in [('L',1),('R',-1)]:
                for i in range(2):
                    p=[(s*.26,-.41,1.04),(s*.28,-.49,.54),(s*.27,-.43,.17),(s*.27,-.62,.055)] if i==0 else [
                       (s*.26,.44,.94),(s*.31,.69,.55),(s*.30,.40,.19),(s*.30,.20,.055)]
                    endpoints[(side,i)]=p
                    for k,n in enumerate(['upper','lower','foot']):
                        parent='body' if k==0 else f'leg{i+1}_{["upper","lower"][k-1]}.{side}'
                        bones.append((f'leg{i+1}_{n}.{side}',p[k],p[k+1],parent))
        elif family=='spider':
            bones += [('body',(0,.15,.70),(0,-.30,.7),'root'),
                      ('abdomen',(0,.15,.70),(0,.77,.80),'body'),
                      ('head',(0,-.34,.67),(0,-.72,.64),'body'),
                      ('jaw',(0,-.62,.62),(0,-.85,.51),'head')]
            for side,s in [('L',1),('R',-1)]:
                for i in range(4):
                    y=-.47+i*.25
                    p=[(s*.24,y,.71),(s*(.77+(.12 if i in [1,2] else 0)),y+(i-1.5)*.23,1.08),
                       (s*(1.10+(.20 if i in [1,2] else 0)),y+(i-1.5)*.48,.035)]
                    endpoints[(side,i)]=p
                    bones.append((f'leg{i+1}_upper.{side}',p[0],p[1],'body'))
                    bones.append((f'leg{i+1}_lower.{side}',p[1],p[2],f'leg{i+1}_upper.{side}'))
                bones.append((f'fang.{side}',(s*.12,-.66,.61),(s*.15,-.90,.42),'head'))
        else:
            bones += [('pelvis',(0,0,1.65),(0,.035,1.90),'root'),
                      ('spine',(0,.035,1.90),(0,-.08,2.22),'pelvis'),
                      ('chest',(0,-.08,2.22),(0,-.16,2.55),'spine'),
                      ('neck',(0,-.16,2.55),(0,-.28,2.69),'chest'),
                      ('head',(0,-.28,2.69),(0,-.32,3.04),'neck'),
                      ('jaw',(0,-.35,2.77),(0,-.58,2.70),'head')]
            for side,s in [('L',1),('R',-1)]:
                armpts=[(s*.51,-.12,2.48),(s*.83,-.18,1.76),(s*.93,-.32,1.03),(s*.93,-.43,.81)]
                legpts=[(s*.27,0,1.68),(s*.37,-.12,.89),(s*.4,.04,.17),(s*.4,-.30,.065)]
                endpoints[(side,'arm')]=armpts
                endpoints[(side,'leg')]=legpts
                for k,n in enumerate(['upper_arm','forearm','hand']):
                    bones.append((n+'.'+side,armpts[k],armpts[k+1],
                                  'chest' if k==0 else ['upper_arm','forearm'][k-1]+'.'+side))
                for k,n in enumerate(['thigh','shin','foot']):
                    bones.append((n+'.'+side,legpts[k],legpts[k+1],
                                  'pelvis' if k==0 else ['thigh','shin'][k-1]+'.'+side))
        arm=C.rig(family.title()+'_v1',bones,col)
        parts={}
        def part(name,slot,m):
            o=m.finish(family.title()+'_'+name,col,arm,slot)
            o['seam_standard']=family.title()+'_v1'
            parts[name]=o
        if family=='animal':
            m=C.Mesh()
            m.ell((0,.13,.94),(.34,.68,.36),'InfectedFlesh','body',12,6)
            m.ell((0,-.35,1.01),(.35,.27,.36),'CharcoalCloth','body',10,5)
            for s in [-1,1]:
                for j in range(5):
                    y=-.25+j*.16
                    m.tube([(s*.08,y,1.23),(s*.31,y,1.10),(s*.29,y,.84)],
                           [.027,.022,.012],'OldBone','body',7)
            part('Torso_ExposedRibs','body',m)
            m=C.Mesh()
            m.ell((0,-.73,1.13),(.20,.32,.20),'InfectedFlesh','head',10,5)
            m.ell((0,-1.00,1.08),(.135,.23,.13),'AshSkin','head',10,4)
            m.ell((0,-1.03,.975),(.13,.20,.043),'InfectedFlesh','jaw',8,4)
            m.ell((0,-1.205,1.09),(.09,.03,.045),'Black','head',8,3)
            for s in [-1,1]:
                m.ell((s*.154,-.86,1.205),(.027,.026,.019),'Amber','head',8,3)
                m.tube([(s*.14,-.65,1.27),(s*.18,-.60,1.48),(s*.13,-.60,1.39)],
                       [.060,.019,.001],'CharcoalCloth','head',7)
                for y in [-.91,-1.02,-1.12]:
                    m.tube([(s*.11,y,1.037),(s*.10,y,.983)],[.017,.001],'OldBone','head',6)
            part('Head_Hound','head',m)
            m=C.Mesh()
            for (side,i),p in endpoints.items():
                u=f'leg{i+1}_upper.{side}'
                l=f'leg{i+1}_lower.{side}'
                f=f'leg{i+1}_foot.{side}'
                m.tube(p[:3],[.12,.069,.037],'InfectedFlesh',[u,{u:.5,l:.5},l],9)
                m.ell(p[1],(.076,.079,.084),'OldBone',l,8,4)
                m.tube(p[2:],[.041,.064],'CharcoalCloth',[f,f],8)
                m.ell(p[3],(.082,.112,.051),'CharcoalCloth',f,8,4)
                for j in range(3):
                    x=p[3][0]+(j-1)*.041
                    m.tube([(x,p[3][1]-.055,.06),(x,p[3][1]-.14,.023)],
                           [.014,.001],'OldBone',f,5)
            part('Limbs_Hound','limbs',m)
            m=C.Mesh()
            m.tube([(0,.60,.97),(0,1.12,.70),(0,1.53,.61)], [.085,.052,.008],
                   'InfectedFlesh',['tail',{'tail':.5,'tail.tip':.5},'tail.tip'],8)
            part('Tail','tail',m)
            m=C.Mesh()
            for s in [-1,1]:
                m.tube([(s*.13,-.70,1.28),(s*.18,-.54,1.52),(s*.29,-.48,1.74)],
                       [.055,.033,.001],'OldBone','head',7)
            for i in range(7):
                y=-.33+i*.14
                m.tube([(0,y,1.25),(0,y+.07,1.51)], [.043,.001],'OldBone','body',6)
            part('Danger_AntlersAndSpine','growth',m)
            m=C.Mesh()
            for s in [-1,1]:
                for i in range(3):
                    m.ell((s*.28,.20+i*.14,1.13),(.13,.14,.12),'Mycelium','body',9,4)
                m.tube([(s*.13,-.64,1.29),(s*.23,-.46,1.60),(s*.38,-.40,1.89),
                        (s*.46,-.49,1.98)], [.065,.05,.026,.001],'OldBone','head',8)
                for y in [-.41,-.27]:
                    m.tube([(s*.30,-.43,1.80),(s*.46,y,1.94)], [.026,.001],'OldBone','head',6)
            m.ell((0,.48,1.26),(.21,.22,.16),'InfectedFlesh','body',10,4)
            part('Boss_HiveCrown','growth',m)
            presets={'Weak_StarvedHound':(['Torso_ExposedRibs','Head_Hound','Limbs_Hound','Tail'],.80),
                     'Danger_ThornHound':(['Torso_ExposedRibs','Head_Hound','Limbs_Hound','Tail','Danger_AntlersAndSpine'],1),
                     'Boss_HiveHart':(['Torso_ExposedRibs','Head_Hound','Limbs_Hound','Tail','Boss_HiveCrown'],1.65)}
        elif family=='spider':
            m=C.Mesh()
            m.ell((0,-.16,.71),(.32,.41,.22),'Chitin','body',12,6)
            m.ell((0,.46,.82),(.46,.60,.40),'Chitin','abdomen',12,6)
            for i in range(5):
                y=.08+i*.17
                m.ell((0,y,.96+.15*math.sin(i*.7)),(.37,.065,.13),'RustedIron','abdomen',10,4)
            part('Body_Carapace','body',m)
            m=C.Mesh()
            m.ell((0,-.61,.66),(.235,.20,.17),'Chitin','head',10,5)
            for s in [-1,1]:
                for i in range(3):
                    m.ell((s*(.05+i*.062),-.775+i*.024,.72-i*.017),
                          (.023,.021,.024),'Amber','head',8,4)
                m.tube([(s*.13,-.69,.61),(s*.19,-.85,.49),(s*.10,-.95,.45)],
                       [.057,.033,.002],'OldBone',f'fang.{"L" if s>0 else "R"}',8)
            part('Head_SixEyes','head',m)
            m=C.Mesh()
            for (side,i),p in endpoints.items():
                u=f'leg{i+1}_upper.{side}'
                l=f'leg{i+1}_lower.{side}'
                m.tube(p,[.072,.052,.006],'Chitin',[u,{u:.5,l:.5},l],8)
                m.ell(p[0],(.085,.08,.08),'InfectedFlesh',u,8,4)
                m.ell(p[1],(.064,.060,.069),'OldBone',l,8,4)
                mid=Vector(p[0]).lerp(Vector(p[1]),.5)
                m.tube([mid,mid+Vector((0,.01,.13))],[.026,.001],'OldBone',u,6)
            part('Legs_Eight','limbs',m)
            m=C.Mesh()
            for s in [-1,1]:
                for i in range(4):
                    m.tube([(s*.30,.15+i*.17,1.02),(s*.48,.17+i*.18,1.26)],
                           [.046,.001],'OldBone','abdomen',6)
            part('Danger_ThornShell','growth',m)
            m=C.Mesh()
            for i in range(9):
                a=i*2.399
                x=.31*math.cos(a)
                y=.44+.39*math.sin(a)
                m.ell((x,y,1.10),(.095,.11,.14),'InfectedFlesh','abdomen',9,4)
                m.ell((x,y,1.21),(.035,.035,.025),'Mycelium','abdomen',8,3)
            for s in [-1,1]:
                m.tube([(s*.12,-.1,.92),(s*.19,-.01,1.29),(s*.17,.1,1.48)],
                       [.055,.045,.001],'OldBone','body',8)
            part('Boss_BroodSacs','growth',m)
            presets={'Weak_AshWeaver':(['Body_Carapace','Head_SixEyes','Legs_Eight'],.65),
                     'Danger_ThornWeaver':(['Body_Carapace','Head_SixEyes','Legs_Eight','Danger_ThornShell'],1),
                     'Boss_BroodMatriarch':(['Body_Carapace','Head_SixEyes','Legs_Eight','Boss_BroodSacs'],1.65)}
        else:
            m=C.Mesh()
            m.loft([(0,0,1.52,.30,.23),(0,.04,1.84,.35,.26),(0,-.04,2.13,.36,.25),
                    (0,-.12,2.43,.58,.30),(0,-.16,2.57,.37,.23)],'InfectedFlesh',
                   ['pelvis',{'pelvis':.5,'spine':.5},'spine','chest','chest'],14)
            for s in [-1,1]:
                for i in range(5):
                    z=2.04+i*.082
                    m.tube([(s*.07,-.29,z),(s*.31,-.31,z+.06),(s*.42,-.20,z+.1)],
                           [.032,.034,.018],'OldBone','chest' if i>1 else 'spine',7)
            m.ell((0,.26,2.36),(.22,.17,.30),'CharcoalCloth','chest',10,5)
            part('Body_RibGiant','body',m)
            m=C.Mesh()
            m.ell((0,-.32,2.85),(.22,.22,.29),'AshSkin','head',12,6)
            m.ell((0,-.50,2.73),(.17,.10,.095),'Black','jaw',10,4)
            for s in [-1,1]:
                m.ell((s*.092,-.51,2.94),(.051,.018,.021),'Black','head',8,4)
                m.ell((s*.092,-.529,2.944),(.016,.008,.010),'Amber','head',8,3)
                for i in range(3):
                    x=s*(.028+i*.045)
                    m.tube([(x,-.565,2.80),(x,-.573,2.737)],[.017,.001],'OldBone','head',6)
            m.ell((0,-.53,2.88),(.037,.044,.065),'AshSkin','head',8,4)
            part('Head_Hollow','head',m)
            m=C.Mesh()
            for side,s in [('L',1),('R',-1)]:
                p=endpoints[(side,'arm')]
                u,l,h='upper_arm.'+side,'forearm.'+side,'hand.'+side
                m.tube(p,[.23,.15,.10,.11],'InfectedFlesh',[u,{u:.5,l:.5},l,h],10)
                m.ell(p[0],(.27,.27,.25),'CharcoalCloth',u,10,5)
                for i in range(4):
                    x=s*(.86+i*.055)
                    m.tube([(x,-.42,.93),(x,-.50,.73),(x,-.56,.68)],
                           [.034,.025,.001],'AshSkin',h,6)
                p=endpoints[(side,'leg')]
                u,l,f='thigh.'+side,'shin.'+side,'foot.'+side
                m.tube(p[:3],[.21,.13,.08],'InfectedFlesh',[u,{u:.5,l:.5},l],10)
                m.ell(p[1],(.15,.17,.14),'OldBone',l,9,4)
                m.ell((s*.4,-.13,.10),(.16,.30,.105),'CharcoalCloth',f,10,4)
                for i in range(3):
                    m.tube([(s*.4+(i-1)*.09,-.32,.11),(s*.4+(i-1)*.09,-.48,.03)],
                           [.029,.001],'OldBone',f,6)
            part('Limbs_Elongated','limbs',m)
            m=C.Mesh()
            for s in [-1,1]:
                v=[(s*.02,-.25,1.74),(s*.30,-.25,1.74),(s*.44,-.29,.96),
                   (s*.05,-.30,.91),(s*.02,.25,1.74),(s*.30,.25,1.74),
                   (s*.44,.29,.96),(s*.05,.30,.91)]
                m.add(v,[(0,1,2,3),(4,7,6,5)],'BloodCloth',
                      [{'pelvis':1},{'pelvis':1},{'thigh.'+('L' if s>0 else 'R'):1},
                       {'thigh.'+('L' if s>0 else 'R'):1}]*2)
            part('Rags','cloth',m)
            m=C.Mesh()
            for s in [-1,1]:
                for i in range(3):
                    m.tube([(s*.48,-.09+i*.12,2.55),(s*.64,-.08+i*.12,2.94)],
                           [.08,.001],'OldBone','chest',7)
            m.tube([(-.92,-.28,1.36),(-1.11,-.32,1.05),(-1.12,-.35,.67)],
                   [(.14,.07),(.13,.05),(.001,.001)],'OldBone','forearm.R',6)
            part('Danger_ShoulderThorns','growth',m)
            m=C.Mesh()
            for i in range(7):
                a=i*2*math.pi/7
                m.tube([(.17*math.cos(a),-.28+.15*math.sin(a),3.00),
                        (.23*math.cos(a),-.25+.20*math.sin(a),3.30+(.10 if i%2==0 else 0))],
                       [.045,.001],'OldBone','head',7)
            for s in [-1,1]:
                m.ell((s*.42,.14,2.56),(.25,.24,.20),'Mycelium','chest',10,5)
                for i in range(3):
                    m.ell((s*.36,.31,2.35+i*.13),(.075,.07,.085),'InfectedFlesh','chest',8,4)
            m.tube([(-.87,-.3,1.48),(-1.12,-.35,1.02),(-1.18,-.40,.59)],
                   [.13,.18,.025],'RustedIron','forearm.R',8)
            part('Boss_SaintCrown','growth',m)
            presets={'Weak_HollowThrall':(['Body_RibGiant','Head_Hollow','Limbs_Elongated','Rags'],.85),
                     'Danger_RibBrute':(['Body_RibGiant','Head_Hollow','Limbs_Elongated','Rags','Danger_ShoulderThorns'],1.1),
                     'Boss_CrownedAbomination':(['Body_RibGiant','Head_Hollow','Limbs_Elongated','Rags','Boss_SaintCrown'],1.5)}
        C.animate(arm,family)
        for n,o in parts.items():
            p=C.ROOT/'Creatures'/family.title()/'Modules'/(n+'.glb')
            C.export_glb(p,[arm,o])
            C.asset_record(family+'_'+n,[o],p,'creature_module',slot=o['module_slot'],rig=arm['rig_id'])
        for index,(n,(names,size)) in enumerate(presets.items()):
            arm.scale=(size,)*3
            objects=[parts[x] for x in names]
            p=C.ROOT/'Creatures'/family.title()/(n+'.glb')
            C.export_glb(p,[arm]+objects,True)
            tier=['weak','danger','boss'][index]
            C.asset_record(n,objects,p,'creature',family=family,tier=tier,modules=names,
                           rig=arm['rig_id'],root_scale=size)
        arm.scale=(1,1,1)
        default=next(v[0] for n,v in presets.items() if n.startswith('Danger'))
        for n,o in parts.items():
            o.hide_set(n not in default)
            o.hide_render=n not in default
        arm.animation_data.action=next(a for a in C.bpy.data.actions if a.name=='Idle')
        C.select_only([arm]+[parts[x] for x in default])
        C.render_setup()
        C.stage_camera('Review',(5,-8,5),(0,0,1.2),5)
        C.save_blend(C.ROOT/'Creatures'/family.title()/(family.title()+'_Modular_Library.blend'))
        # Review three tiers in one scene with independent armatures.
        for o in parts.values():
            o.hide_render=True
            o.hide_set(True)
        stage=C.collection('Tier_Review')
        spacing=3.9 if family!='giant' else 3.3
        for i,(n,(names,size)) in enumerate(presets.items()):
            a=arm.copy()
            a.data=arm.data.copy()
            a.animation_data_clear()
            stage.objects.link(a)
            a.scale=(size,)*3
            a.location.x=(i-1)*spacing
            a.rotation_euler.z=-.12
            for x in names:
                ob=parts[x].copy()
                stage.objects.link(ob)
                ob.parent=a
                ob.modifiers['Skin'].object=a
                ob.hide_render=False
                ob.hide_set(False)
        C.floor_mesh(stage)
        C.stage_camera('Tier_Lineup',(5,-12,7),(0,0,1.3 if family!='giant' else 2),
                       13 if family!='giant' else 12)
        C.render(C.ROOT/'Previews'/('02_'+family.title()+'.png'))
        C.save_blend(C.ROOT/'Creatures'/family.title()/(family.title()+'_Tier_Lineup.blend'))
        print('CREATURE_FAMILY_COMPLETE',family,flush=True)
