"""Blender-only asset authoring. No game source or repository configuration is accessed."""
import bpy
import math
import json
import random
import sys
from pathlib import Path
from mathutils import Vector, Quaternion
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RNG = random.Random(20261021)
FPS = 30
MANIFEST = {'version': 1, 'units': 'metres', 'blender_up': 'Z', 'blender_forward': '-Y',
            'gltf_up': 'Y', 'triangle_limit_assembled_character': 9999,
            'assets': [], 'animations': {}, 'materials': []}
MATS = {}

PALETTE = {
    'AshSkin': ((0.39, 0.36, 0.31), .83, 0),
    'PaleSkin': ((0.57, 0.53, 0.43), .8, 0),
    'DarkSkin': ((0.25, 0.21, 0.18), .84, 0),
    'CharcoalCloth': ((0.12, 0.135, 0.13), .95, 0),
    'FadedLinen': ((0.43, 0.40, 0.31), .98, 0),
    'BloodCloth': ((0.25, 0.10, 0.085), .89, 0),
    'WornLeather': ((0.23, 0.15, 0.095), .82, 0),
    'RustedIron': ((0.23, 0.20, 0.16), .72, .7),
    'TarnishedBrass': ((0.38, 0.29, 0.13), .61, .65),
    'OldBone': ((0.64, 0.59, 0.44), .76, 0),
    'DeadWood': ((0.24, 0.20, 0.145), .95, 0),
    'SootStone': ((0.30, 0.32, 0.30), .97, 0),
    'Plaster': ((0.46, 0.43, 0.34), .98, 0),
    'Slate': ((0.16, 0.20, 0.22), .87, 0),
    'GraveSoil': ((0.21, 0.18, 0.125), 1, 0),
    'WitheredGrass': ((0.30, 0.31, 0.16), .96, 0),
    'ContaminatedSoil': ((0.18, 0.22, 0.15), .83, 0),
    'Mycelium': ((0.43, 0.44, 0.28), .72, 0),
    'InfectedFlesh': ((0.34, 0.22, 0.18), .68, 0),
    'Chitin': ((0.135, 0.18, 0.17), .58, .1),
    'Black': ((0.024, 0.029, 0.028), .78, 0),
    'Amber': ((0.53, 0.31, 0.095), .38, .05),
}

def new_scene(name):
    for o in list(bpy.context.scene.objects):
        bpy.data.objects.remove(o,do_unlink=True)
    for a in list(bpy.data.actions):
        bpy.data.actions.remove(a)
    for c in list(bpy.data.collections):
        if c.name != 'Collection' and c.users == 0:
            bpy.data.collections.remove(c)
    bpy.context.scene.name = name
    bpy.context.scene.unit_settings.system = 'METRIC'
    bpy.context.scene.render.fps = FPS
    bpy.context.scene.world.color = (.12, .12, .12)

def collection(name):
    c = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(c)
    return c

def relocate(o, c):
    for old in list(o.users_collection):
        old.objects.unlink(o)
    c.objects.link(o)

def make_materials():
    size = 512
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    for index, (name, (color, rough, metal)) in enumerate(PALETTE.items()):
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        mat.diffuse_color = (*color, 1)
        nodes = mat.node_tree.nodes
        bsdf = nodes.get('Principled BSDF')
        bsdf.inputs['Metallic'].default_value = metal
        bsdf.inputs['Roughness'].default_value = rough
        prng = np.random.default_rng(index + 892)
        h = np.zeros((size, size), np.float32)
        for freq, amp in [(2, .32), (5, .16), (13, .08), (31, .035)]:
            for wave in range(5):
                kx=int(prng.integers(-freq,freq+1))
                ky=int(prng.integers(-freq,freq+1))
                if kx==0 and ky==0:
                    kx=1
                h += amp*.4*np.sin(2*np.pi*(kx*x+ky*y+prng.random()))
        h += (prng.random((size, size))-.5)*.055
        if 'Wood' in name:
            h += .13*np.sin(2*np.pi*(x*38+.3*np.sin(y*4*np.pi)))
        if 'Cloth' in name or 'Linen' in name:
            h += .055*(np.sin(x*256*np.pi)+np.sin(y*256*np.pi))
        if name in ['SootStone', 'Slate', 'Plaster']:
            h -= .10*(np.abs(np.sin(x*8*np.pi+.6*np.sin(y*4*np.pi)))>.985)
        if name in ['Mycelium', 'ContaminatedSoil', 'InfectedFlesh', 'Chitin']:
            h += .12*np.sin(2*np.pi*(x*9+.5*np.sin(y*6*np.pi)))**8
        h = np.clip(.5+h, 0, 1)
        base = np.clip(np.array(color)[None,None,:]*(.73+.50*h[:,:,None]), 0, 1)
        if name == 'RustedIron':
            rust = np.clip((h-.43)*2.2, 0, .55)[:,:,None]
            base = base*(1-rust)+np.array([.36,.14,.055])*rust
        dy, dx = np.gradient(h)
        norm = np.stack((-dx*5, -dy*5, np.ones_like(h)), -1)
        norm /= np.linalg.norm(norm, axis=-1, keepdims=True)
        maps = {'BaseColor': base,
                'Roughness': np.repeat(np.clip(rough+(h-.5)*.16,.05,1)[:,:,None],3,axis=2),
                'Normal': norm*.5+.5,
                'Height': np.repeat(h[:,:,None],3,axis=2)}
        imgs = {}
        target = ROOT/'Materials'/name
        target.mkdir(exist_ok=True)
        for kind, rgb in maps.items():
            rgba = np.concatenate([rgb, np.ones((size,size,1))], axis=-1).astype(np.float32)
            im = bpy.data.images.new(name+'_'+kind, width=size, height=size, alpha=False)
            if kind != 'BaseColor':
                im.colorspace_settings.name = 'Non-Color'
            im.pixels.foreach_set(rgba.ravel())
            im.filepath_raw = str(target/(kind+'.png'))
            im.file_format = 'PNG'
            im.save()
            im.pack()
            imgs[kind] = im
        for kind, socket in [('BaseColor','Base Color'), ('Roughness','Roughness')]:
            tex = nodes.new('ShaderNodeTexImage')
            tex.image = imgs[kind]
            mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs[socket])
        tex = nodes.new('ShaderNodeTexImage')
        tex.image = imgs['Normal']
        normal = nodes.new('ShaderNodeNormalMap')
        normal.inputs['Strength'].default_value = .45
        mat.node_tree.links.new(tex.outputs['Color'], normal.inputs['Color'])
        mat.node_tree.links.new(normal.outputs['Normal'], bsdf.inputs['Normal'])
        MATS[name] = mat
        MANIFEST['materials'].append({'id': name, 'size': 512, 'metallic': metal,
                                      'maps': [str((target/(k+'.png')).relative_to(ROOT)) for k in maps]})

class Mesh:
    def __init__(self):
        self.v, self.f, self.mi, self.w = [], [], [], []
        self.materials = []

    def add(self, verts, faces, material, weights=None):
        start = len(self.v)
        self.v.extend([tuple(v) for v in verts])
        if isinstance(weights, str):
            weights = {weights: 1.0}
        if isinstance(weights, dict) or weights is None:
            weights = [weights or {}]*len(verts)
        self.w.extend(weights)
        if material not in self.materials:
            self.materials.append(material)
        mi = self.materials.index(material)
        self.f.extend([tuple(start+i for i in f) for f in faces])
        self.mi.extend([mi]*len(faces))

    def tube(self, points, radii, mat, weights=None, sides=8):
        vs, ws, fs = [], [], []
        for i, (p, radius) in enumerate(zip(points,radii)):
            p = Vector(p)
            tangent = Vector(points[min(i+1,len(points)-1)])-Vector(points[max(0,i-1)])
            tangent.normalize()
            axis = Vector((0,1,0))
            if abs(tangent.dot(axis)) > .95:
                axis = Vector((1,0,0))
            a = tangent.cross(axis).normalized()
            b = tangent.cross(a).normalized()
            rx, ry = radius if isinstance(radius,(tuple,list)) else (radius,radius)
            weight = weights[i] if isinstance(weights,list) else weights
            if isinstance(weight,str):
                weight = {weight:1}
            for j in range(sides):
                angle = j*2*math.pi/sides
                vs.append(p+a*(rx*math.cos(angle))+b*(ry*math.sin(angle)))
                ws.append(weight or {})
            if i:
                for j in range(sides):
                    k = (j+1)%sides
                    fs.append(((i-1)*sides+j,(i-1)*sides+k,i*sides+k,i*sides+j))
        fs.append(tuple(reversed(range(sides))))
        fs.append(tuple((len(points)-1)*sides+j for j in range(sides)))
        self.add(vs, fs, mat, ws)

    def loft(self, rings, mat, weights=None, sides=12):
        vs, ws, fs = [], [], []
        for i, (x,y,z,rx,ry) in enumerate(rings):
            weight = weights[i] if isinstance(weights,list) else weights
            if isinstance(weight,str):
                weight = {weight:1}
            for j in range(sides):
                a = 2*math.pi*j/sides
                vs.append((x+rx*math.cos(a),y+ry*math.sin(a),z))
                ws.append(weight or {})
            if i:
                for j in range(sides):
                    k = (j+1)%sides
                    fs.append(((i-1)*sides+j,(i-1)*sides+k,i*sides+k,i*sides+j))
        fs.append(tuple(reversed(range(sides))))
        fs.append(tuple((len(rings)-1)*sides+j for j in range(sides)))
        self.add(vs, fs, mat, ws)

    def ell(self, center, scale, mat, bone=None, sides=10, rings=5, rotation=None):
        vs = [(0,0,-1)]
        for i in range(1,rings):
            t = math.pi*i/rings
            for j in range(sides):
                a = 2*math.pi*j/sides
                vs.append((math.sin(t)*math.cos(a),math.sin(t)*math.sin(a),-math.cos(t)))
        vs.append((0,0,1))
        fs = []
        for j in range(sides):
            fs.append((0,1+(j+1)%sides,1+j))
        for i in range(rings-2):
            for j in range(sides):
                a = 1+i*sides+j
                b = 1+i*sides+(j+1)%sides
                fs.append((a,b,b+sides,a+sides))
        top = len(vs)-1
        for j in range(sides):
            a=1+(rings-2)*sides+j
            b=1+(rings-2)*sides+(j+1)%sides
            fs.append((a,b,top))
        verts = []
        for p in vs:
            q = Vector((p[0]*scale[0],p[1]*scale[1],p[2]*scale[2]))
            if rotation:
                q = rotation @ q
            verts.append(q+Vector(center))
        self.add(verts,fs,mat,bone)

    def beam(self,a,b,width,depth,mat,bone=None):
        self.tube([a,b],[(width/2,depth/2)]*2,mat,bone,4)

    def box(self,center,size,mat,bone=None):
        x,y,z=center
        a,b,c=[q/2 for q in size]
        v=[(x+i*a,y+j*b,z+k*c) for k in [-1,1] for j in [-1,1] for i in [-1,1]]
        self.add(v,[(0,2,3,1),(4,5,7,6),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)],mat,bone)

    def finish(self,name,col,arm=None,slot=None):
        mesh=bpy.data.meshes.new(name)
        mesh.from_pydata(self.v,[],self.f)
        mesh.update()
        o=bpy.data.objects.new(name,mesh)
        col.objects.link(o)
        for m in self.materials:
            mesh.materials.append(MATS[m])
        for poly,idx in zip(mesh.polygons,self.mi):
            poly.material_index=idx
        uv=mesh.uv_layers.new(name='UVMap')
        for poly in mesh.polygons:
            axis=max(range(3),key=lambda k: abs(poly.normal[k]))
            axes=[k for k in range(3) if k!=axis]
            for li in poly.loop_indices:
                p=mesh.vertices[mesh.loops[li].vertex_index].co
                uv.data[li].uv=(p[axes[0]]*.5,p[axes[1]]*.5)
        if arm:
            names=set(k for w in self.w for k in w)
            for name in names:
                g=o.vertex_groups.new(name=name)
                for i,w in enumerate(self.w):
                    if w.get(name,0)>0:
                        g.add([i],w[name],'REPLACE')
            mod=o.modifiers.new('Skin','ARMATURE')
            mod.object=arm
            o.parent=arm
            o['rig_id']=arm['rig_id']
        if slot:
            o['module_slot']=slot
        o['triangles']=sum(len(f)-2 for f in self.f)
        o['license']='Original procedural artwork; created for this project'
        return o

def rig(name,bones,col):
    data=bpy.data.armatures.new(name)
    arm=bpy.data.objects.new(name,data)
    col.objects.link(arm)
    bpy.context.view_layer.objects.active=arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for n,h,t,parent in bones:
        b=data.edit_bones.new(n)
        b.head=h
        b.tail=t
        if parent:
            b.parent=data.edit_bones[parent]
        b.use_deform=not n.startswith('socket')
    bpy.ops.object.mode_set(mode='OBJECT')
    arm.select_set(False)
    arm.show_in_front=True
    arm['rig_id']=name
    arm['forward']='-Y'
    for p in arm.pose.bones:
        p.rotation_mode='QUATERNION'
    return arm

def world_rotation(arm,name,rot):
    if name not in arm.pose.bones:
        return
    b=arm.data.bones[name]
    q=Quaternion((1,0,0),rot[0]) @ Quaternion((0,1,0),rot[1]) @ Quaternion((0,0,1),rot[2])
    basis=b.matrix_local.to_quaternion()
    arm.pose.bones[name].rotation_quaternion=basis.inverted() @ q @ basis

def global_offset(arm,name,z):
    arm.pose.bones[name].location=arm.data.bones[name].matrix_local.to_quaternion().inverted() @ Vector((0,0,z))

def pose_world_quaternion(arm,name):
    b=arm.data.bones[name]
    rest=b.matrix_local.to_quaternion()
    if b.parent:
        relative=b.parent.matrix_local.to_quaternion().inverted() @ rest
        return pose_world_quaternion(arm,b.parent.name) @ relative @ arm.pose.bones[name].rotation_quaternion
    return rest @ arm.pose.bones[name].rotation_quaternion

def point_bone(arm,name,direction):
    b=arm.data.bones[name]
    rest=b.matrix_local.to_quaternion()
    delta=(b.tail_local-b.head_local).normalized().rotation_difference(Vector(direction).normalized())
    desired=delta @ rest
    if b.parent:
        relative=b.parent.matrix_local.to_quaternion().inverted() @ rest
        q=relative.inverted() @ pose_world_quaternion(arm,b.parent.name).inverted() @ desired
    else:
        q=rest.inverted() @ desired
    arm.pose.bones[name].rotation_quaternion=q

def gait_ik(arm,kind,phase,blend):
    """Bake two-link IK to plain FK quaternion keys. No runtime constraint dependency."""
    human=kind in ['human','giant']
    size=1.8 if kind=='giant' else 1
    base='pelvis' if human else 'body'
    lowering=(-.10-.045*blend)*size if human else (-.10 if kind=='animal' else -.015)
    bob=(.012+.02*blend)*math.sin(phase*2)*size
    dz=lowering+bob
    global_offset(arm,base,dz)
    legs=1 if human else 4 if kind=='spider' else 2
    for side in ['L','R']:
        for i in range(legs):
            angle=phase+(math.pi if side=='R' else 0)+(i%2)*math.pi
            u=(angle/(2*math.pi))%1
            stance=.60-.20*blend
            length=(.68+.19*blend)*size if human else (.48+.16*blend) if kind=='animal' else (.34+.10*blend)
            if u<stance:
                forward=-length/2+length*u/stance
                lift=0
            else:
                v=(u-stance)/(1-stance)
                forward=length/2-length*(v*v*(3-2*v))
                lift=(.12+.12*blend)*size*math.sin(math.pi*v)
            upper='thigh.'+side if human else f'leg{i+1}_upper.{side}'
            lower='shin.'+side if human else f'leg{i+1}_lower.{side}'
            b1,b2=arm.data.bones[upper],arm.data.bones[lower]
            hip=b1.head_local+Vector((0,0,dz))
            target=b2.tail_local+Vector((0,forward,lift))
            delta=target-hip
            distance=min(delta.length,b1.length+b2.length-.00001)
            direction=delta.normalized()
            a=(b1.length*b1.length-b2.length*b2.length+distance*distance)/(2*distance)
            h=math.sqrt(max(0,b1.length*b1.length-a*a))
            pole=(b1.tail_local-b1.head_local)-direction*(b1.tail_local-b1.head_local).dot(direction)
            if pole.length<.001:
                pole=Vector((0,-1,0)) if human else Vector((0,0,1))
                pole-=direction*pole.dot(direction)
            # Humanoid knees consistently bend forward, including at the zero-stride sample.
            if human:
                pole=Vector((0,-1,0))-direction*Vector((0,-1,0)).dot(direction)
            knee=hip+direction*a+pole.normalized()*h
            point_bone(arm,upper,knee-hip)
            point_bone(arm,lower,target-knee)
            foot='foot.'+side if human else f'leg{i+1}_foot.{side}'
            if foot in arm.data.bones:
                rest=arm.data.bones[foot]
                point_bone(arm,foot,rest.tail_local-rest.head_local)

CLIPS = {
    'BindPose_A': (2,False,0), 'Idle':(60,True,0), 'Walk':(32,True,1.25),
    'Run':(24,True,3.4), 'TurnLeft90':(20,False,0), 'TurnRight90':(20,False,0),
    'Attack':(30,False,0), 'Jump':(36,False,0), 'WalkToRun':(18,False,0),
    'RunToWalk':(20,False,0), 'Hit':(18,False,0), 'Death':(48,False,0)
}

def gait_speed(kind,clip):
    factor=1.8 if kind=='giant' else 1
    distance=(.68 if clip=='Walk' else .87) if kind in ['human','giant'] else (
        (.48 if clip=='Walk' else .64) if kind=='animal' else (.34 if clip=='Walk' else .44))
    duration=(31 if clip=='Walk' else 23)/FPS
    stance=.6 if clip=='Walk' else .4
    return distance*factor/(duration*stance)

def animate(arm,kind,scale=1):
    arm.animation_data_create()
    for clip,(end,loop,speed) in CLIPS.items():
        speed=gait_speed(kind,clip) if clip in ['Walk','Run'] else 0
        act=bpy.data.actions.new(clip)
        act.use_fake_user=True
        arm.animation_data.action=act
        act['loop']=loop
        act['nominal_speed_mps']=speed*scale
        act['in_place']=True
        for frame in range(1,end+1):
            t=(frame-1)/(end-1)
            for p in arm.pose.bones:
                p.rotation_quaternion=(1,0,0,0)
                p.location=(0,0,0)
                p.scale=(1,1,1)
            move=clip in ['Walk','Run','WalkToRun','RunToWalk']
            running=clip=='Run'
            blend=t if clip=='WalkToRun' else (1-t if clip=='RunToWalk' else float(running))
            phase=2*math.pi*t
            stride=.46+.35*blend
            base='pelvis' if kind in ['human','giant'] else 'body'
            if clip=='Idle':
                world_rotation(arm,base,(.009*math.sin(phase),0,.008*math.sin(phase)))
                if 'chest' in arm.pose.bones:
                    arm.pose.bones['chest'].scale=(1+.008*math.sin(phase),1+.012*math.sin(phase),1)
            if move:
                global_offset(arm,base,(.012+.018*blend)*math.sin(phase*2)*scale)
                if kind in ['human','giant']:
                    for side,off in [('L',0),('R',math.pi)]:
                        wave=math.sin(phase+off)
                        lift=max(0,-wave)
                        world_rotation(arm,'thigh.'+side,(wave*stride,0,0))
                        world_rotation(arm,'shin.'+side,(-lift*(.67+.55*blend),0,0))
                        world_rotation(arm,'foot.'+side,(lift*.28-wave*.10,0,0))
                        world_rotation(arm,'upper_arm.'+side,(-wave*stride*.7,0,0))
                        world_rotation(arm,'forearm.'+side,(-.12-blend*.55,0,0))
                    world_rotation(arm,'chest',(.05+.08*blend,0,.045*math.sin(phase)))
                    world_rotation(arm,'head',(-.03-.05*blend,0,-.025*math.sin(phase)))
                    if 'cape' in arm.pose.bones:
                        world_rotation(arm,'cape',(-.13-blend*.2+.07*math.sin(phase),0,.03*math.sin(phase)))
                else:
                    legs=4 if kind=='spider' else 2
                    for side,s in [('L',1),('R',-1)]:
                        for i in range(legs):
                            # Alternating tetrapod for spiders; diagonal trot for quadrupeds.
                            off=(i%2)*math.pi+(0 if side=='L' else math.pi)
                            wave=math.sin(phase+off)
                            world_rotation(arm,f'leg{i+1}_upper.{side}',(wave*stride*.55,0,wave*.14*s))
                            world_rotation(arm,f'leg{i+1}_lower.{side}',(-max(0,-wave)*.85,0,0))
                            if kind=='animal':
                                world_rotation(arm,f'leg{i+1}_foot.{side}',(max(0,-wave)*.35,0,0))
                    world_rotation(arm,'head',(.025*math.sin(phase*2),0,0))
                    if 'tail' in arm.pose.bones:
                        world_rotation(arm,'tail',(0,.10*math.sin(phase),.1*math.sin(phase)))
                gait_ik(arm,kind,phase,blend)
            if clip.startswith('Turn'):
                direction=1 if clip=='TurnLeft90' else -1
                world_rotation(arm,'root',(0,0,direction*math.pi*.5*(t*t*(3-2*t))))
                world_rotation(arm,base,(0,0,.1*direction*math.sin(math.pi*t)))
                if kind in ['human','giant']:
                    for side,s in [('L',1),('R',-1)]:
                        world_rotation(arm,'thigh.'+side,(s*.22*math.sin(t*4*math.pi),0,0))
                        world_rotation(arm,'shin.'+side,(-max(0,s*math.sin(t*4*math.pi))*.35,0,0))
                else:
                    for side,s in [('L',1),('R',-1)]:
                        for i in range(4 if kind=='spider' else 2):
                            a=math.sin(t*4*math.pi+i*math.pi+(0 if s>0 else math.pi))
                            world_rotation(arm,f'leg{i+1}_upper.{side}',(a*.24,0,a*.14))
                            world_rotation(arm,f'leg{i+1}_lower.{side}',(-max(0,-a)*.5,0,0))
            if clip=='Attack':
                wind=math.sin(math.pi*min(t/.38,1)) if t<.38 else 0
                strike=max(0,math.sin(math.pi*(t-.38)/.62)) if t>=.38 else 0
                if kind in ['human','giant']:
                    world_rotation(arm,'chest',(-wind*.2+strike*.38,0,wind*-.35+strike*.32))
                    world_rotation(arm,'upper_arm.R',(-wind*1.3+strike*.65,-wind*.4,-wind*.25))
                    world_rotation(arm,'forearm.R',(-wind*.8-strike*.25,0,0))
                    world_rotation(arm,'upper_arm.L',(-wind*.35-strike*.25,0,-.12*strike))
                    world_rotation(arm,'head',(-strike*.12,0,0))
                else:
                    world_rotation(arm,base,(-wind*.12+strike*.23,0,0))
                    world_rotation(arm,'head',(wind*.25-strike*.38,0,0))
                    if 'jaw' in arm.pose.bones:
                        world_rotation(arm,'jaw',(.45*(wind+strike*.3),0,0))
                    if kind=='spider':
                        for side,s in [('L',1),('R',-1)]:
                            world_rotation(arm,f'leg1_upper.{side}',(-wind*.8+strike*.9,0,s*wind*.2))
                            world_rotation(arm,f'fang.{side}',(strike*.5,0,0))
            if clip=='Jump':
                # Pose curve only; explicit vertical motion is on pelvis/body, never root translation.
                lift=max(0,math.sin(math.pi*max(0,min(1,(t-.20)/.62))))
                crouch=math.sin(math.pi*t/.20)*.11 if t<.20 else 0
                global_offset(arm,base,(lift*.52-crouch)*scale)
                if kind in ['human','giant']:
                    for side in ['L','R']:
                        world_rotation(arm,'thigh.'+side,(-lift*.6-crouch*3,0,0))
                        world_rotation(arm,'shin.'+side,(-lift*.9-crouch*5,0,0))
                        world_rotation(arm,'upper_arm.'+side,(-lift*.55,0,0))
                else:
                    for side in ['L','R']:
                        for i in range(4 if kind=='spider' else 2):
                            world_rotation(arm,f'leg{i+1}_lower.{side}',(-lift*.6,0,0))
            if clip=='Hit':
                a=math.sin(math.pi*t)*(1-t)
                world_rotation(arm,base,(-.23*a,0,.09*a))
                world_rotation(arm,'head',(-.24*a,0,0))
            if clip=='Death':
                a=min(1,t/.78)
                world_rotation(arm,base,(a*1.32,0,a*.18))
                global_offset(arm,base,-a*(.78 if kind in ['human','giant'] else .38)*scale)
                if kind in ['human','giant']:
                    world_rotation(arm,'thigh.L',(-a*.6,0,0))
                    world_rotation(arm,'thigh.R',(-a*.9,0,0))
                    world_rotation(arm,'shin.L',(-a*1.0,0,0))
                    world_rotation(arm,'shin.R',(-a*.8,0,0))
            for p in arm.pose.bones:
                if p.name.startswith('socket'):
                    continue
                p.keyframe_insert('rotation_quaternion',frame=frame,group=p.name)
                p.keyframe_insert('location',frame=frame,group=p.name)
                p.keyframe_insert('scale',frame=frame,group=p.name)
        track=arm.animation_data.nla_tracks.new()
        track.name=clip
        strip=track.strips.new(clip,1,act)
        strip.action_frame_start=1
        strip.action_frame_end=end
        track.mute=True
    arm.animation_data.action=None
    bpy.context.scene.frame_set(1)
    for p in arm.pose.bones:
        p.rotation_quaternion=(1,0,0,0)
        p.location=(0,0,0)
        p.scale=(1,1,1)
    MANIFEST['animations'][arm['rig_id']]={n:{'frames':[1,e], 'fps':FPS,'loop':l,
        'nominal_speed_mps':gait_speed(kind,n)*scale if n in ['Walk','Run'] else 0,
        'root_yaw_degrees':90 if n=='TurnLeft90' else -90 if n=='TurnRight90' else 0}
        for n,(e,l,v) in CLIPS.items()}

def select_only(objects):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:
        o.hide_set(False)
        o.hide_viewport=False
        o.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]

def export_glb(path,objects,animated=False):
    path.parent.mkdir(parents=True,exist_ok=True)
    select_only(objects)
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
        export_animations=animated,export_animation_mode='ACTIONS',export_nla_strips=True,
        export_force_sampling=True,export_frame_range=False,export_anim_single_armature=False,
        export_skins=True,export_extras=True,export_yup=True,export_cameras=False,
        export_lights=False,export_apply=False,export_image_format='AUTO')

def save_blend(path):
    bpy.ops.wm.save_as_mainfile(filepath=str(path),compress=True)

def asset_record(name,objects,path,category,**extra):
    tris=sum(o.get('triangles',0) for o in objects if o.type=='MESH')
    record={'id':name,'category':category,'triangles':tris,'file':str(path.relative_to(ROOT)),**extra}
    MANIFEST['assets'].append(record)
    if category in ['character_preset','creature'] and tris>=10000:
        raise RuntimeError(f'{name}: {tris} triangles exceeds budget')
    return record

def stage_camera(name,location,target,ortho):
    data=bpy.data.cameras.new(name)
    cam=bpy.data.objects.new(name,data)
    bpy.context.scene.collection.objects.link(cam)
    cam.location=location
    cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler()
    data.type='ORTHO'
    data.ortho_scale=ortho
    bpy.context.scene.camera=cam
    return cam

def render_setup():
    scene=bpy.context.scene
    if scene.world is None:
        scene.world=bpy.data.worlds.new('ResourcePreviewWorld')
    scene.render.engine='CYCLES'
    scene.cycles.samples=24
    scene.cycles.use_denoising=True
    scene.render.resolution_x=1600
    scene.render.resolution_y=1050
    scene.render.resolution_percentage=100
    scene.view_settings.view_transform='AgX'
    scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.105,.125,.145,1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value=.45
    for name,loc,power,color,size in [('Key',(-4,-6,9),1700,(1,.82,.62),7),
            ('Fill',(6,-2,5),1000,(.57,.73,.88),6),('Rim',(1,6,8),2100,(.81,.87,.65),5)]:
        d=bpy.data.lights.new(name,'AREA')
        d.energy=power
        d.color=color
        d.shape='DISK'
        d.size=size
        o=bpy.data.objects.new(name,d)
        scene.collection.objects.link(o)
        o.location=loc
        o.rotation_euler=(-o.location).to_track_quat('-Z','Y').to_euler()

def render(path):
    bpy.context.scene.render.filepath=str(path)
    bpy.ops.render.render(write_still=True)

def floor_mesh(col,size=30):
    m=Mesh()
    m.box((0,0,-.085),(size,size,.12),'CharcoalCloth')
    return m.finish('PreviewFloor',col)

def finish_manifest():
    (ROOT/'Documentation/asset_manifest.json').write_text(json.dumps(MANIFEST,ensure_ascii=False,indent=2),encoding='utf-8')

if __name__=='__main__':
    make_materials()
    # Builders are loaded as authoring modules, never as game runtime dependencies.
    sys.path.insert(0,str(ROOT/'Authoring'))
    import build_characters
    build_characters.build(sys.modules[__name__])
    finish_manifest()
    import build_creatures
    build_creatures.build(sys.modules[__name__])
    finish_manifest()
    import build_environment
    build_environment.build(sys.modules[__name__])
    finish_manifest()
    print('RESOURCE_BUILD_COMPLETE',flush=True)
