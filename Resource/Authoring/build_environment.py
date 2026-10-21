import math
from mathutils import Vector

def build(C):
    C.new_scene('Environment_Modular_Library')
    col=C.collection('Architecture_2m_Grid')
    props=C.collection('Household_and_Ritual')
    nature=C.collection('Nature_and_Contamination')
    assets={}
    def store(n,m,category='architecture',pivot='ground_center',arm=None):
        dest=col if category=='architecture' else props if category=='prop' else nature
        ob=m.finish(n,dest,arm)
        ob['grid_m']=2.0 if category=='architecture' else .25
        ob['pivot']=pivot
        path=C.ROOT/'Environment'/category.title()/(n+'.glb')
        C.export_glb(path,([arm] if arm else [])+[ob],arm is not None)
        C.asset_record(n,[ob],path,category,grid_m=ob['grid_m'],pivot=pivot,rig=arm['rig_id'] if arm else None)
        assets[n]=(ob,arm,category)
        return ob
    def stone(m,center,size):
        m.ell(center,size,'SootStone',sides=7,rings=3)
    def plank(m,center,size,mat='DeadWood'):
        m.box(center,size,mat)
    def small_animation(arm,clip,length,bone,axis,angle=0,translation=0,pulse=False):
        arm.animation_data_create()
        a=C.bpy.data.actions.new(arm.name+'_'+clip)
        arm.animation_data.action=a
        a.use_fake_user=True
        for f in range(1,length+1):
            t=(f-1)/(length-1)
            v=math.sin(t*math.pi*2) if clip in ['Wind','Pulse'] else (1-t if clip=='Close' else t)
            r=[0,0,0]
            r[axis]=v*angle
            C.world_rotation(arm,bone,r)
            p=arm.pose.bones[bone]
            p.location.z=v*translation
            p.scale=(1+v*.04,1+v*.04,1+v*.08) if pulse else (1,1,1)
            p.keyframe_insert('rotation_quaternion',frame=f)
            p.keyframe_insert('location',frame=f)
            p.keyframe_insert('scale',frame=f)
        tr=arm.animation_data.nla_tracks.new()
        tr.name=clip
        tr.strips.new(clip,1,a)
        tr.mute=True
        arm.animation_data.action=None
        for p in arm.pose.bones:
            p.rotation_quaternion=(1,0,0,0)
            p.location=(0,0,0)
            p.scale=(1,1,1)
        C.MANIFEST['animations'].setdefault(arm['rig_id'],{})[clip]={
            'frames':[1,length],'fps':30,'loop':clip in ['Wind','Pulse']}

    for n,mat in [('Floor_Wood_2m','DeadWood'),('Floor_Stone_2m','SootStone')]:
        m=C.Mesh()
        if mat=='DeadWood':
            for i in range(8):
                m.box((-.875+i*.25,0,-.065),(.24,2,.13),mat)
                for y in [-.85,.85]:
                    m.ell((-.875+i*.25,y,.005),(.012,.012,.004),'RustedIron',sides=6,rings=3)
        else:
            for x in range(4):
                for y in range(4):
                    m.box((-.75+x*.5,-.75+y*.5,-.055),(.48,.48,.11),mat)
        store(n,m)
    for variant in ['Plaster','Timber','Stone','Window','Doorway','Ruined']:
        m=C.Mesh()
        if variant in ['Plaster','Timber']:
            m.box((0,0,1.2),(2,.16,2.4),'Plaster' if variant=='Plaster' else 'DeadWood')
            if variant=='Timber':
                for i in range(9):
                    m.box((-.9+i*.225,-.092,1.2),(.017,.015,2.35),'Black')
        if variant=='Stone':
            for i in range(6):
                for j in range(4):
                    x=-.75+j*.5+(0.15 if i%2 else 0)
                    w=min(.48,2*(1-abs(x)))
                    if w>0:
                        m.box((x,0,.20+i*.4),(w,.24,.38),'SootStone')
        if variant=='Window':
            for center,size in [((-.75,0,1.2),(.5,.16,2.4)),((.75,0,1.2),(.5,.16,2.4)),
                ((0,0,.45),(1,.16,.9)),((0,0,2.15),(1,.16,.5))]:
                m.box(center,size,'Plaster')
            for x in [-.51,.51]:
                m.box((x,-.025,1.4),(.10,.22,1.08),'DeadWood')
            for z in [.89,1.92]:
                m.box((0,-.025,z),(1.15,.22,.10),'DeadWood')
            for x in [-.28,0,.28]:
                m.box((x,0,1.41),(.018,.018,.95),'RustedIron')
        if variant=='Doorway':
            for x in [-.75,.75]:
                m.box((x,0,1.2),(.5,.20,2.4),'Plaster')
                m.box((x/1.5,-.02,1.02),(.12,.25,2.04),'DeadWood')
            m.box((0,0,2.23),(1,.20,.34),'Plaster')
            m.box((0,-.02,2.02),(1.12,.25,.13),'DeadWood')
        if variant=='Ruined':
            for i in range(6):
                h=[1.0,1.6,.8,.5,1.3,2.1][i]
                m.box((-.83+i*.33,0,h/2),(.32,.22,h),'SootStone')
            for i in range(5):
                stone(m,(-.7+i*.3,-.19,.10),(.16,.14,.1))
        for x in [-.94,.94]:
            m.box((x,-.015,1.2),(.12,.24,2.4),'DeadWood')
        for z in [.08,2.32]:
            m.box((0,-.015,z),(2,.24,.12),'DeadWood')
        if variant in ['Plaster','Timber']:
            m.beam((-.89,-.14,.14),(.89,-.14,2.25),.10,.10,'DeadWood')
        store('Wall_'+variant+'_2m',m)
    for n,h in [('Post_2m4',2.4),('Post_1m2',1.2)]:
        m=C.Mesh()
        m.box((0,0,h/2),(.20,.20,h),'DeadWood')
        for z in [.12,h-.12]:
            m.box((0,0,z),(.23,.23,.07),'RustedIron')
        store(n,m)
    m=C.Mesh()
    m.box((0,0,.12),(.34,.34,.24),'SootStone')
    store('Foundation_Corner',m)
    for n,mat in [('Roof_Slate_2m','Slate'),('Roof_Wood_2m','DeadWood')]:
        m=C.Mesh()
        # Local eave is y=-1,z=0; ridge is y=1,z=1.2. Pivot is eave midpoint.
        m.add([(-1,-1,0),(1,-1,0),(1,1,1.2),(-1,1,1.2),
               (-1,-1,-.08),(1,-1,-.08),(1,1,1.12),(-1,1,1.12)],
              [(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],mat)
        for row in range(6):
            for j in range(6):
                x=-.83+j*.333
                y=-.87+row*.34
                m.box((x,y,(y+1)*.6+.025),(.315,.31,.046),mat)
        for x in [-.94,.94]:
            m.beam((x,-1,-.1),(x,1,1.10),.12,.12,'DeadWood')
        store(n,m,pivot='roof_patch_center_xy_eave_z')
    m=C.Mesh()
    m.tube([(-1,0,0),(1,0,0)],[.115,.115],'Slate',sides=6)
    store('Roof_Ridge_2m',m)
    m=C.Mesh()
    m.add([(-1,0,0),(1,0,0),(0,0,1.2),(-1,.14,0),(1,.14,0),(0,.14,1.2)],
          [(0,1,2),(3,5,4),(0,3,4,1),(1,4,5,2),(2,5,3,0)],'Plaster')
    for a,b in [((-1,-.03,0),(0,-.03,1.2)),((0,-.03,1.2),(1,-.03,0))]:
        m.beam(a,b,.11,.11,'DeadWood')
    store('Gable_Triangle_2m',m)
    m=C.Mesh()
    for z in range(5):
        m.box((0,0,.20+z*.36),(.60,.60,.34),'SootStone')
    for x in [-.25,.25]:
        m.box((x,0,1.9),(.12,.65,.2),'SootStone')
    store('Chimney',m)
    m=C.Mesh()
    for i in range(5):
        m.box((0,-.8+i*.4,-.08+i*.16),(2,.4,.16*(i+1)),'SootStone')
    store('Stairs_2m',m)
    m=C.Mesh()
    for x in [-1,1]:
        m.box((x,0,.58),(.12,.12,1.16),'DeadWood')
    for z in [.40,.85]:
        m.box((0,0,z),(2,.09,.10),'DeadWood')
    store('Fence_2m',m)
    m=C.Mesh()
    for x in [-.79,.79]:
        m.box((x,0,1.0),(.42,.55,2),'SootStone')
    # Segmented pointed arch with a clear opening.
    for side,s in [('L',1),('R',-1)]:
        for i in range(5):
            z=1.88+i*.15
            x=s*(.61-i*.115)
            m.box((x,0,z),(.28,.55,.21),'SootStone')
    store('Arch_Ruined_2m',m)
    m=C.Mesh()
    m.box((0,0,.12),(1.2,.8,.24),'SootStone')
    m.box((0,0,.67),(.42,.38,1.1),'SootStone')
    m.ell((0,-.04,1.37),(.26,.18,.37),'OldBone',sides=9,rings=4)
    m.tube([(0,.02,1.65),(0,.04,2.13)],[.075,.005],'OldBone',sides=7)
    store('Shrine_MutantSaint',m)
    # Hinged door: origin at left hinge, opening fits the .88m doorway.
    arm=C.rig('Door_Hinge_v1',[('root',(0,0,0),(0,0,.2),None),
               ('hinge',(-.44,0,0),(-.44,0,1.9),'root')],props)
    for clip in ['Open','Close']:
        small_animation(arm,clip,36,'hinge',2,math.pi*.55)
    m=C.Mesh()
    for i in range(5):
        m.box((-.352+i*.176,0,.96),(.17,.075,1.92),'DeadWood','hinge')
    for z in [.29,1.56]:
        m.box((0,-.045,z),(.88,.032,.09),'RustedIron','hinge')
    m.ell((.30,-.07,1.00),(.035,.025,.035),'TarnishedBrass','hinge',8,4)
    store('Door_Hinged',m,'prop',arm=arm)
    for n,size in [('Table',(.95,.68,.76)),('Bench',(1.25,.35,.47)),('Stool',(.38,.38,.45))]:
        m=C.Mesh()
        w,d,h=size
        for i in range(4):
            m.box((-w/2+w*(i+.5)/4,0,h-.04),(w/4-.008,d,.08),'DeadWood')
        for x in [-w*.38,w*.38]:
            for y in [-d*.35,d*.35]:
                m.beam((x,y,0),(x*.92,y*.9,h-.07),.075,.075,'DeadWood')
        m.box((0,0,h*.38),(w*.8,.06,.065),'DeadWood')
        store(n,m,'prop')
    m=C.Mesh()
    for x in [-.19,.19]:
        for y in [-.18,.18]:
            m.box((x,y,.23),(.065,.065,.46),'DeadWood')
        m.box((x,.18,.70),(.06,.06,.56),'DeadWood')
    m.box((0,0,.46),(.45,.44,.065),'DeadWood')
    for z in [.68,.89]:
        m.box((0,.18,z),(.44,.06,.07),'DeadWood')
    store('Chair',m,'prop')
    m=C.Mesh()
    for x in [-.43,.43]:
        for y in [-.86,.86]:
            m.box((x,y,.22),(.09,.09,.44),'DeadWood')
    m.box((0,0,.35),(.94,1.9,.15),'DeadWood')
    m.ell((0,.05,.48),(.44,.88,.14),'FadedLinen',sides=8,rings=4)
    m.box((0,.15,.56),(.86,1.45,.045),'CharcoalCloth')
    m.ell((0,-.60,.59),(.32,.20,.09),'FadedLinen',sides=8,rings=4)
    store('Bed_Straw',m,'prop')
    for n,dim in [('Crate_Small',.55),('Crate_Large',.90)]:
        m=C.Mesh()
        m.box((0,0,dim/2),(dim,dim,dim),'DeadWood')
        for side in [-1,1]:
            for a in [-.37,.37]:
                m.box((a*dim,side*(dim/2+.016),dim/2),(.07,.04,dim),'DeadWood')
            m.beam((-dim*.40,side*(dim/2+.035),.07),(dim*.40,side*(dim/2+.035),dim-.07),.065,.05,'DeadWood')
        store(n,m,'prop')
    m=C.Mesh()
    m.loft([(0,0,0,.25,.25),(0,0,.1,.28,.28),(0,0,.42,.31,.31),
            (0,0,.75,.28,.28),(0,0,.82,.25,.25)],'DeadWood',sides=12)
    for z,r in [(.10,.287),(.35,.315),(.70,.297)]:
        m.loft([(0,0,z,r,r),(0,0,z+.04,r,r)],'RustedIron',sides=12)
    store('Barrel',m,'prop')
    arm=C.rig('Chest_Hinge_v1',[('root',(0,0,0),(0,0,.2),None),
                 ('lid',(0,.25,.40),(.2,.25,.40),'root')],props)
    for clip in ['Open','Close']:
        small_animation(arm,clip,30,'lid',0,-math.pi*.55)
    m=C.Mesh()
    for x in [-.37,.37]:
        m.box((x,0,.20),(.06,.56,.4),'DeadWood','root')
    for y in [-.25,.25]:
        m.box((0,y,.20),(.8,.06,.4),'DeadWood','root')
    m.box((0,0,.035),(.8,.56,.07),'DeadWood','root')
    m.box((0,0,.44),(.83,.59,.09),'DeadWood','lid')
    for x in [-.27,.27]:
        m.box((x,0,.50),(.055,.60,.035),'RustedIron','lid')
    m.box((0,-.295,.39),(.08,.025,.1),'TarnishedBrass','root')
    store('Chest_Hinged',m,'prop',arm=arm)
    m=C.Mesh()
    for x in [-.35,.35]:
        m.box((x,0,.85),(.08,.28,1.70),'DeadWood')
    for z in [.08,.55,1.10,1.66]:
        m.box((0,0,z),(.76,.32,.07),'DeadWood')
    store('Shelf',m,'prop')
    m=C.Mesh()
    m.ell((0,0,.23),(.27,.26,.24),'RustedIron',sides=12,rings=6)
    m.loft([(0,0,.35,.22,.22),(0,0,.39,.22,.22)],'RustedIron',sides=12)
    m.ell((0,0,.38),(.19,.19,.008),'Black',sides=12,rings=3)
    for s in [-1,1]:
        m.tube([(s*.24,0,.27),(s*.33,0,.33),(s*.25,0,.39)], [.021]*3,'RustedIron',sides=6)
    store('Cauldron',m,'prop')
    m=C.Mesh()
    m.loft([(0,0,0,.10,.10),(0,0,.04,.13,.13),(0,0,.21,.14,.14)],'DeadWood',sides=10)
    for z in [.03,.18]:
        m.loft([(0,0,z,.143,.143),(0,0,z+.018,.143,.143)],'RustedIron',sides=10)
    m.tube([(-.13,0,.20),(-.11,0,.36),(.11,0,.36),(.13,0,.20)], [.012]*4,'RustedIron',sides=6)
    store('Bucket',m,'prop')
    for n,shape,mat in [('Bowl',(.14,.14,.055),'DeadWood'),('Plate',(.18,.18,.018),'SootStone')]:
        m=C.Mesh()
        m.loft([(0,0,0,shape[0]*.5,shape[1]*.5),(0,0,shape[2],shape[0],shape[1])],mat,sides=12)
        m.ell((0,0,shape[2]+.002),(shape[0]*.85,shape[1]*.85,.005),'Black',sides=12,rings=3)
        store(n,m,'prop')
    m=C.Mesh()
    m.tube([(0,0,.025),(0,0,.30)],[.026,.025],'Amber',sides=8)
    m.tube([(0,0,.28),(0,0,.34)],[.016,.016],'OldBone',sides=8)
    m.loft([(0,0,.10,.028,.028),(0,0,.19,.028,.028)],'WornLeather',sides=8)
    store('BloodVial',m,'prop')
    m=C.Mesh()
    m.box((0,0,.16),(.21,.21,.25),'Amber')
    for x in [-.12,.12]:
        for y in [-.12,.12]:
            m.box((x,y,.17),(.025,.025,.29),'RustedIron')
    for z in [.02,.31]:
        m.box((0,0,z),(.29,.29,.045),'RustedIron')
    m.tube([(-.08,0,.34),(-.08,0,.47),(.08,0,.47),(.08,0,.34)], [.014]*4,'RustedIron',sides=6)
    store('Lantern',m,'prop')
    m=C.Mesh()
    m.tube([(0,0,0),(0,0,.65)],[.025,.019],'DeadWood',sides=8)
    m.box((0,0,.68),(.25,.08,.09),'RustedIron')
    store('WorkHammer',m,'prop')
    m=C.Mesh()
    m.tube([(0,0,0),(0,0,1.2)],[.025,.025],'DeadWood',sides=8)
    m.ell((0,-.025,.16),(.14,.035,.19),'RustedIron',sides=8,rings=4)
    store('Shovel',m,'prop')
    m=C.Mesh()
    for x in [-.40,.40]:
        m.box((x,0,.40),(.25,.50,.80),'SootStone')
    m.box((0,0,.86),(1.25,.65,.16),'SootStone')
    m.box((0,0,.98),(.70,.38,.06),'BloodCloth')
    m.ell((0,0,1.10),(.17,.13,.14),'OldBone',sides=9,rings=4)
    store('Altar',m,'prop')
    m=C.Mesh()
    m.box((0,0,.085),(.45,.30,.17),'DeadWood')
    m.box((0,-.155,.1),(.09,.015,.12),'TarnishedBrass')
    for x in [-.19,.19]:
        m.box((x,0,.18),(.025,.30,.02),'TarnishedBrass')
    store('Reliquary',m,'prop')
    m=C.Mesh()
    m.ell((0,0,.35),(.25,.23,.35),'FadedLinen',sides=9,rings=5)
    m.tube([(0,0,.59),(0,0,.73)],[.06,.04],'FadedLinen',sides=8)
    m.loft([(0,0,.62,.065,.065),(0,0,.65,.065,.065)],'WornLeather',sides=8)
    store('Sack',m,'prop')
    m=C.Mesh()
    for i in range(6):
        a=2*math.pi*i/6
        stone(m,(.35*math.cos(a),.35*math.sin(a),.06),(.14,.14,.08))
    for i in range(3):
        a=i*math.pi/3
        m.tube([(-.26*math.cos(a),-.26*math.sin(a),.08),(.26*math.cos(a),.26*math.sin(a),.12)],
               [.05,.04],'DeadWood',sides=7)
    store('ColdHearth',m,'prop')

    # Terrain tiles: planar boundary guarantees modular joins; relief stays inside the border.
    for n,mat in [('Ground_GraveSoil','GraveSoil'),('Ground_Contaminated','ContaminatedSoil'),
                 ('Ground_Stone','SootStone'),('Ground_Mycelium','Mycelium')]:
        m=C.Mesh()
        v=[]
        for j in range(9):
            for i in range(9):
                x,y=-1+i*.25,-1+j*.25
                z=.032*math.sin(i*2.13)*math.cos(j*1.9)*math.sin(math.pi*i/8)*math.sin(math.pi*j/8)
                v.append((x,y,z))
        f=[]
        for j in range(8):
            for i in range(8):
                a=j*9+i
                f.append((a,a+1,a+10,a+9))
        m.add(v,f,mat)
        store(n,m,'nature')
    for n,k in [('Rock_Small',.40),('Rock_Medium',.85),('Rock_Large',1.60),('Rock_Spire',1.0)]:
        m=C.Mesh()
        if n=='Rock_Spire':
            m.loft([(0,0,0,.5,.4),(.02,0,.5,.38,.33),(-.07,0,1.6,.23,.22),
                    (.09,.02,2.3,.06,.05)],'SootStone',sides=7)
        else:
            m.ell((0,0,k*.38),(k*.65,k*.50,k*.49),'SootStone',sides=7,rings=4)
            m.ell((k*.30,k*.10,k*.21),(k*.33,k*.27,k*.26),'SootStone',sides=6,rings=3)
        store(n,m,'nature')
    m=C.Mesh()
    for i in range(9):
        a=i*2.4
        r=.16+.18*(i%3)
        stone(m,(r*math.cos(a),r*math.sin(a),.06),(.11,.13,.08))
    store('Rubble_Cluster',m,'nature')
    # Shared nature skeleton per asset: actual vertex weights and skeletal wind/pulse actions.
    for n,height,infected in [('Tree_Dead_A',3.6,False),('Tree_Dead_B',2.8,False),
                             ('Tree_Infected',3.5,True)]:
        arm=C.rig(n+'_Wind_v1',[('root',(0,0,0),(0,0,.2),None),
                    ('trunk',(0,0,.15),(0,0,height*.6),'root'),
                    ('crown',(0,0,height*.6),(0,0,height),'trunk')],nature)
        small_animation(arm,'Wind',90,'crown',1,.025)
        m=C.Mesh()
        m.tube([(0,0,0),(.05,0,.5),(-.04,.02,height*.58),(.08,.04,height*.85),(.02,.07,height)],
               [.30,.22,.14,.07,.002],'DeadWood',
               ['trunk','trunk',{'trunk':.5,'crown':.5},'crown','crown'],9)
        for i in range(7):
            a=i*2.4
            z=height*(.42+.065*i)
            base=Vector((0,0,z))
            end=Vector((math.cos(a)*(.8 if i<4 else .6),math.sin(a)*(.8 if i<4 else .6),z+.55))
            bone='trunk' if i<2 else 'crown'
            m.tube([base,base.lerp(end,.65),end],[.09,.044,.004],'DeadWood',bone,7)
            fork=base.lerp(end,.65)
            m.tube([fork,fork+Vector((-.25*math.sin(a),.25*math.cos(a),.42))],
                   [.035,.001],'DeadWood',bone,6)
        for i in range(6):
            a=i*math.pi/3
            m.tube([(0,0,.15),(.34*math.cos(a),.34*math.sin(a),.06),
                    (.65*math.cos(a),.65*math.sin(a),.015)], [.14,.09,.001],'DeadWood','trunk',7)
        if infected:
            for i in range(7):
                z=.65+i*.30
                x=.12*math.cos(i*2.4)
                y=.12*math.sin(i*2.4)
                m.ell((x,y,z),(.17,.17,.15),'Mycelium','trunk' if z<2 else 'crown',9,4)
                m.ell((x*1.5,y*1.5,z+.04),(.09,.09,.11),'InfectedFlesh','trunk' if z<2 else 'crown',8,4)
        store(n,m,'nature',arm=arm)
    m=C.Mesh()
    m.loft([(0,0,0,.28,.24),(.02,0,.42,.22,.20),(0,0,.63,.23,.20)],'DeadWood',sides=9)
    m.ell((0,0,.62),(.17,.15,.012),'Black',sides=9,rings=3)
    for i in range(4):
        a=i*math.pi/2
        m.tube([(0,0,.1),(.5*math.cos(a),.5*math.sin(a),.01)], [.12,.001],'DeadWood',sides=7)
    store('Tree_Stump',m,'nature')
    m=C.Mesh()
    m.tube([(-.9,0,.15),(0,0,.19),(.9,0,.15)], [.18,.20,.16],'DeadWood',sides=9)
    m.tube([(.1,0,.19),(.2,.4,.55)],[.09,.001],'DeadWood',sides=7)
    store('Fallen_Log',m,'nature')
    for n,kind in [('Grass_Dry_Clump','dry'),('Grass_Reeds','reed'),('Grass_Fungal','fungal')]:
        arm=C.rig(n+'_Wind_v1',[('root',(0,0,0),(0,0,.05),None),
                    ('foliage',(0,0,.06),(0,0,.55),'root')],nature)
        small_animation(arm,'Wind',72,'foliage',0,.10)
        m=C.Mesh()
        for i in range(15 if kind!='reed' else 11):
            a=i*2.399
            r=.09+.12*(i%3)/3
            x,y=math.cos(a)*r,math.sin(a)*r
            h=.32+.10*(i%4)
            if kind=='reed':
                h*=1.7
                m.tube([(x,y,0),(x+.05,y,h)], [.009,.005],'WitheredGrass',['root','foliage'],5)
                m.ell((x+.05,y,h),(.025,.025,.11),'WornLeather','foliage',6,3)
            else:
                dx,dy=math.cos(a)*.035,math.sin(a)*.035
                v=[(x-dx,y-dy,0),(x+dx,y+dy,0),(x+dx*2,y+dy*2,h*.55),
                   (x+dx*3,y+dy*3,h)]
                m.add(v,[(0,1,2),(0,2,3),(2,1,0),(3,2,0)],
                      'Mycelium' if kind=='fungal' else 'WitheredGrass',
                      [{'root':1},{'root':1},{'foliage':1},{'foliage':1}])
        store(n,m,'nature',arm=arm)
    for n,k in [('Colony_Small',.65),('Colony_Large',1.25)]:
        arm=C.rig(n+'_Pulse_v1',[('root',(0,0,0),(0,0,.1),None),
                    ('colony',(0,0,.1),(0,0,.7),'root')],nature)
        small_animation(arm,'Pulse',120,'colony',0,.018,pulse=True)
        m=C.Mesh()
        for i in range(13):
            a=i*2.399
            r=k*.38*math.sqrt(i/13)
            x,y=r*math.cos(a),r*math.sin(a)
            h=k*(.22+.10*(i%4))
            m.tube([(x,y,0),(x*.9,y*.9,h*.7)], [.045*k,.035*k],'InfectedFlesh','colony',7)
            m.ell((x*.9,y*.9,h*.8),(.12*k,.13*k,.10*k),'Mycelium','colony',9,4)
            if i%3==0:
                m.ell((x*.9,y*.9,h*.9),(.04*k,.04*k,.03*k),'Amber','colony',7,3)
        for i in range(7):
            a=i*2*math.pi/7
            m.tube([(0,0,.05),(k*.4*math.cos(a),k*.4*math.sin(a),.025),
                    (k*.65*math.cos(a),k*.65*math.sin(a),.008)],
                   [.04*k,.022*k,.001],'Mycelium','root',6)
        store(n,m,'nature',arm=arm)
    m=C.Mesh()
    for i in range(8):
        a=i*2.399
        p=(.3*math.cos(a),.3*math.sin(a),.16+.05*(i%3))
        m.ell(p,(.12,.14,.18),'InfectedFlesh',sides=9,rings=4)
        m.ell((p[0],p[1],p[2]+.11),(.045,.05,.035),'Amber',sides=8,rings=3)
    store('Egg_Cluster',m,'nature')
    m=C.Mesh()
    for i in range(6):
        a=i*2.4
        m.tube([(0,0,.10),(.35*math.cos(a),.35*math.sin(a),.045),
                (.85*math.cos(a),.85*math.sin(a),.007)],[.06,.035,.002],
               'Mycelium',sides=6)
    store('Mycelium_Roots',m,'nature')
    # Store a visually organized library. Exports were written at each asset's original origin.
    offsets={'architecture':(0,0),'prop':(0,14),'nature':(0,25)}
    counts={k:0 for k in offsets}
    for n,(o,a,category) in assets.items():
        i=counts[category]
        counts[category]+=1
        x=(i%7)*3.8
        y=offsets[category][1]+(i//7)*3.9
        (a if a else o).location=(x,y,0)
    C.render_setup()
    C.stage_camera('Library_Overview',(32,-35,38),(10,13,.5),40)
    C.save_blend(C.ROOT/'Environment/Environment_Modular_Library.blend')
    C.finish_manifest()
    static_names=[n for n,(o,a,c) in assets.items() if a is None]
    dynamic_arm_names={n:a.name for n,(o,a,c) in assets.items() if a is not None}
    # Example assembled settlement: grid floor, walls with genuine openings, split pitched roof.
    C.new_scene('Settlement_Assembly_Example')
    C.bpy.ops.wm.read_factory_settings(use_empty=True)
    C.bpy.context.scene.name='Settlement_Assembly_Example'
    C.bpy.context.scene.unit_settings.system='METRIC'
    C.bpy.context.scene.render.fps=30
    assembled=C.collection('Settlement_Assembly')
    # Read only this task's own native library to create linked-source assembly examples.
    with C.bpy.data.libraries.load(str(C.ROOT/'Environment/Environment_Modular_Library.blend'),link=False) as (src,dst):
        dst.objects=[n for n in src.objects if n in static_names]
    source={o.name:o for o in dst.objects if o}
    def place(name,loc=(0,0,0),angle=0):
        original=source[name]
        o=original.copy()
        o.data=original.data
        o.animation_data_clear()
        assembled.objects.link(o)
        o.location=loc
        o.rotation_euler=(0,0,angle)
        return o
    for x in [-1,1]:
        for y in [-1,1]:
            place('Floor_Wood_2m',(x,y,.18))
    for x in [-1,1]:
        place('Wall_Window_2m',(x,2,.18))
        place('Wall_Doorway_2m' if x==-1 else 'Wall_Plaster_2m',(x,-2,.18))
    for y in [-1,1]:
        for x in [-2,2]:
            place('Wall_Timber_2m',(x,y,.18),math.pi/2)
    for x in [-1,1]:
        place('Roof_Slate_2m',(x,-1,2.58))
        place('Roof_Slate_2m',(x,1,2.58),math.pi)
        place('Roof_Ridge_2m',(x,0,3.78))
    place('Gable_Triangle_2m',(-2,0,2.58),math.pi/2).scale.x=2
    place('Gable_Triangle_2m',(2,0,2.58),math.pi/2).scale.x=2
    place('Chimney',(1.3,.6,2.58))
    for xy in [(-2,-2),(-2,2),(2,-2),(2,2)]:
        place('Foundation_Corner',(*xy,0))
    place('Stairs_2m',(-1,-3,.0)).scale.z=.1875
    for x in [-5,-3,3,5]:
        place('Fence_2m',(x,-1,0))
    for x in [-1,1]:
        place('Wall_Ruined_2m',(x+6,1,0))
    place('Arch_Ruined_2m',(5,3,0))
    for n,loc in [('Table',(-3,-3,0)),('Chair',(-3,-4,0)),('Barrel',(2.7,-2.2,0)),
                 ('Crate_Large',(3.2,-3.0,0)),('Crate_Small',(3.2,-3,.9)),('ColdHearth',(-4,-.4,0)),
                 ('Bucket',(-3,-2.7,.77)),('Lantern',(-2.9,-3.1,.77)),('Sack',(2.7,-1.4,0)),
                 ('Shrine_MutantSaint',(5.5,1,0)),('Altar',(4.3,-.2,0)),
                 ('Rock_Large',(-5,3.8,0)),('Rock_Spire',(6,5,0)),('Tree_Stump',(-5,-3,0)),
                 ('Fallen_Log',(-5,1.5,0)),('Egg_Cluster',(5,3.7,0)),('Mycelium_Roots',(4,2,0))]:
        place(n,loc)
    for x in range(-4,5):
        for y in range(-3,4):
            place('Ground_Contaminated' if x>1 else 'Ground_GraveSoil',(x*2,y*2,-.025))
    # Dynamic foliage can be appended intact, preserving its independent armature and clips.
    dynamic_names=['Tree_Dead_A','Tree_Dead_B','Tree_Infected','Grass_Dry_Clump',
                   'Grass_Fungal','Colony_Large','Door_Hinged','Chest_Hinged']
    for j,n in enumerate(dynamic_names):
        armname=dynamic_arm_names[n]
        with C.bpy.data.libraries.load(str(C.ROOT/'Environment/Environment_Modular_Library.blend'),link=False) as (src,dst):
            dst.objects=[n,armname]
        ob=next(o for o in dst.objects if o.type=='MESH')
        a=next(o for o in dst.objects if o.type=='ARMATURE')
        for o in [a,ob]:
            assembled.objects.link(o)
        ob.parent=a
        ob.modifiers['Skin'].object=a
        a.location=[(-4,3,0),(-6,0,0),(3.5,4,0),(-3,-4.8,0),(3.7,2.8,0),
                    (5,3,0),(-1,-2.03,.18),(3.8,-3,0)][j]
        if n=='Door_Hinged':
            C.world_rotation(a,'hinge',(0,0,math.pi*.4))
    C.render_setup()
    C.stage_camera('Settlement_QuarterView',(12,-17,13),(0,.5,1),19)
    C.render(C.ROOT/'Previews/03_Settlement.png')
    C.save_blend(C.ROOT/'Environment/Settlement_Assembly_Example.blend')
    C.export_glb(C.ROOT/'Environment/Settlement_Assembly_Example.glb',list(assembled.objects),False)
    print('ENVIRONMENT_COMPLETE',len(assets),flush=True)
