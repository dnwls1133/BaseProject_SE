"""Produce runtime meshes from the repository's authored GLBs; does not launch the game.

Source of truth: Resource/*.glb and PNG. Output: Data/Resources; rerun after asset edits.
The pack contains UVs, skin weights, and shared sampled bone palettes, never CPU-skinned
per-character triangle copies. Animation sampling uses quaternion SLERP at 24 Hz.
"""
from pathlib import Path
import argparse
import bisect
import hashlib
import json
import math
import shutil
import struct

ROOT = Path(__file__).resolve().parents[1]
RESOURCE = ROOT / "Resource"
DATA = ROOT / "SimpleGame" / "Data"
IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


def multiply(a, b):
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4))
            for c in range(4) for r in range(4)]


def transform(m, p, direction=False):
    return [sum(m[c * 4 + r] * p[c] for c in range(3)) +
            (0 if direction else m[12 + r]) for r in range(3)]


def normalized(v):
    length = math.sqrt(sum(x*x for x in v))
    return [x / max(length, 1e-12) for x in v]


def trs(t, q, s):
    x, y, z, w = normalized(q)
    m = [1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w), 0,
         2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w), 0,
         2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y), 0, *t, 1]
    for c in range(3):
        for r in range(3):
            m[c*4+r] *= s[c]
    return m


def slerp(a, b, t):
    dot = sum(x*y for x, y in zip(a, b))
    if dot < 0:
        b, dot = [-x for x in b], -dot
    if dot > .9995:
        return normalized([x+(y-x)*t for x, y in zip(a, b)])
    angle = math.acos(max(-1., min(1., dot)))
    den = math.sin(angle)
    return [(x*math.sin((1-t)*angle)+y*math.sin(t*angle))/den for x, y in zip(a, b)]


class Glb:
    def __init__(self, path):
        raw = path.read_bytes()
        magic, version, length = struct.unpack_from('<III', raw)
        if magic != 0x46546C67 or version != 2 or length != len(raw):
            raise ValueError(f"invalid GLB: {path}")
        at = 12
        self.binary = b''
        while at < len(raw):
            n, kind = struct.unpack_from('<II', raw, at)
            chunk = raw[at+8:at+8+n]
            if kind == 0x4E4F534A:
                self.doc = json.loads(chunk)
            elif kind == 0x004E4942:
                self.binary = chunk
            at += 8+n
        self.cache = {}
        self.nodes = self.doc['nodes']
        self.parents = [-1] * len(self.nodes)
        for parent, node in enumerate(self.nodes):
            for child in node.get('children', []):
                self.parents[child] = parent

    def accessor(self, index):
        if index in self.cache:
            return self.cache[index]
        a = self.doc['accessors'][index]
        if 'sparse' in a:
            raise ValueError('sparse accessor not supported in authored runtime pipeline')
        view = self.doc['bufferViews'][a['bufferView']]
        fmt, size = {5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),
                     5125:('I',4),5126:('f',4)}[a['componentType']]
        count = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        offset = view.get('byteOffset',0)+a.get('byteOffset',0)
        stride = view.get('byteStride', count*size)
        values = [struct.unpack_from('<'+fmt*count,self.binary,offset+i*stride)
                  for i in range(a['count'])]
        if a.get('normalized') and fmt != 'f':
            divisor = {5120:127,5121:255,5122:32767,5123:65535}[a['componentType']]
            values = [tuple(max(-1.,x/divisor) for x in v) for v in values]
        self.cache[index] = values
        return values

    def globals(self, channels=(), time=0):
        local = [[list(n.get('translation',[0,0,0])), list(n.get('rotation',[0,0,0,1])),
                  list(n.get('scale',[1,1,1]))] for n in self.nodes]
        for node, path, times, values, interpolation in channels:
            hi = min(len(times)-1, bisect.bisect_right(times,time))
            lo = max(0,hi-1)
            if time <= times[0]:
                lo = hi = 0
            elif time >= times[-1]:
                lo = hi = len(times)-1
            t = 0 if lo == hi else (time-times[lo])/max(1e-8,times[hi]-times[lo])
            if interpolation == 'STEP':
                value = values[lo]
            elif path == 'rotation':
                value = slerp(values[lo],values[hi],t)
            else:
                value = [a+(b-a)*t for a,b in zip(values[lo],values[hi])]
            local[node][{'translation':0,'rotation':1,'scale':2}[path]] = value
        result = [None]*len(local)
        def visit(index):
            if result[index] is None:
                m = self.nodes[index].get('matrix',trs(*local[index]))
                parent = self.parents[index]
                result[index] = multiply(visit(parent),m) if parent >= 0 else m
            return result[index]
        for index in range(len(local)):
            visit(index)
        return result

    def export(self, path, materials):
        bones, keys = [], {}
        def bone(key, node, inverse):
            if key not in keys:
                keys[key] = len(bones)
                bones.append((node,inverse))
            return keys[key]
        vertices = []
        bounds = [[1e20]*3,[-1e20]*3]
        defaults = self.globals()
        for node_index,node in enumerate(self.nodes):
            if 'mesh' not in node:
                continue
            skin_id = node.get('skin')
            skin = self.doc.get('skins',[])[skin_id] if skin_id is not None else None
            if skin:
                inverses = self.accessor(skin['inverseBindMatrices'])
                remap = [bone((skin_id,j),n,list(inverses[j])) for j,n in enumerate(skin['joints'])]
            else:
                remap = [bone(('node',node_index),node_index,IDENTITY)]
            for primitive in self.doc['meshes'][node['mesh']]['primitives']:
                if primitive.get('mode',4) != 4:
                    raise ValueError('only triangle primitives are supported')
                attrs = primitive['attributes']
                positions = self.accessor(attrs['POSITION'])
                normals = self.accessor(attrs['NORMAL'])
                uvs = self.accessor(attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else [(0,0)]*len(positions)
                joint = self.accessor(attrs['JOINTS_0']) if skin else [(0,0,0,0)]*len(positions)
                weight = self.accessor(attrs['WEIGHTS_0']) if skin else [(1,0,0,0)]*len(positions)
                mat = self.doc.get('materials',[])[primitive.get('material',0)]
                name = mat.get('name','SootStone').split('.')[0]
                if name not in materials:
                    raise ValueError(f'unknown authored material {name}')
                layer, metallic = materials[name]
                pbr = mat.get('pbrMetallicRoughness',{})
                color = pbr.get('baseColorFactor',[1,1,1,1])
                rough = pbr.get('roughnessFactor',1)
                indices = [i[0] for i in self.accessor(primitive['indices'])] if 'indices' in primitive else range(len(positions))
                for index in indices:
                    joints = [remap[int(j)] for j in joint[index]] if skin else [remap[0]]*4
                    weights = [w/max(sum(weight[index]),1e-9) for w in weight[index]]
                    position = positions[index]
                    world = [0.,0.,0.]
                    for j,w in zip(joints,weights):
                        if w:
                            n,ib = bones[j]
                            q = transform(multiply(defaults[n],ib),position)
                            world = [a+b*w for a,b in zip(world,q)]
                    for c in range(3):
                        bounds[0][c] = min(bounds[0][c],world[c])
                        bounds[1][c] = max(bounds[1][c],world[c])
                    vertices.append((*position,*normals[index],*color[:3],float(9 if name in ['Mycelium','InfectedFlesh'] else 0),*uvs[index],float(layer),
                                     *map(float,joints),*weights,float(metallic),float(rough)))
        if not vertices or len(bones)>256:
            raise ValueError('empty mesh or bone palette overflow')
        if not self.doc.get('animations'):
            baked=[]
            for vertex in vertices:
                matrix=multiply(defaults[bones[int(vertex[13])][0]],bones[int(vertex[13])][1])
                position=transform(matrix,vertex[:3])
                normal=normalized(transform(matrix,vertex[3:6],True))
                baked.append((*position,*normal,*vertex[6:13],0.,0.,0.,0.,1.,0.,0.,0.,*vertex[21:]))
            vertices=baked
            bones=[(-1,IDENTITY)]
        clips = [('Rest',1,0.,[])]
        for anim in self.doc.get('animations',[]):
            channels=[]
            duration=0.
            for channel in anim['channels']:
                sampler=anim['samplers'][channel['sampler']]
                if sampler.get('interpolation','LINEAR') not in ['LINEAR','STEP']:
                    raise ValueError('unbaked cubic animation')
                target=channel['target']
                if target['path'] not in ['translation','rotation','scale']:
                    raise ValueError('unsupported morph animation')
                times=[v[0] for v in self.accessor(sampler['input'])]
                values=self.accessor(sampler['output'])
                duration=max(duration,times[-1])
                channels.append((target['node'],target['path'],times,values,sampler.get('interpolation','LINEAR')))
            name=anim.get('name','Idle').split('|')[-1].split('__')[-1]
            for known in ['WalkToRun','RunToWalk','TurnLeft90','TurnRight90','BindPose_A',
                          'Attack','Death','Idle','Jump','Run','Walk','Hit','Wind','Pulse','Open','Close']:
                if name.endswith(known):
                    name=known
                    break
            clips.append((name,max(2,math.ceil(duration*24)+1),duration,channels))
        palettes=[]
        records=[]
        for name,frames,duration,channels in clips:
            start=len(palettes)
            records.append((name,start,frames,duration))
            for frame in range(frames):
                g=self.globals(channels,duration*frame/max(1,frames-1))
                palettes.extend(multiply(g[node],inverse) if node >= 0 else IDENTITY for node,inverse in bones)
        with path.open('wb') as out:
            out.write(struct.pack('<6I6f',0x48534D52,1,len(vertices),len(palettes),len(records),len(bones),
                                  *bounds[0],*bounds[1]))
            for name,start,frames,duration in records:
                encoded=name.encode('utf-8')
                out.write(struct.pack('<I',len(encoded))+encoded+struct.pack('<IIf',start,frames,duration))
            for vertex in vertices:
                out.write(struct.pack('<23f',*vertex))
            for matrix in palettes:
                out.write(struct.pack('<16f',*matrix))
        return len(vertices)//3,len(bones),len(records)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--force',action='store_true')
    args=parser.parse_args()
    out=DATA/'Resources'
    (out/'Meshes').mkdir(parents=True,exist_ok=True)
    manifest=json.loads((RESOURCE/'Documentation/asset_manifest.json').read_text(encoding='utf-8'))
    materials={m['id']:(i,m['metallic']) for i,m in enumerate(manifest['materials'])}
    palette=['version 1']
    for name,(layer,metallic) in materials.items():
        paths=[]
        for kind in ['BaseColor','Normal','Roughness']:
            source=RESOURCE/'Materials'/name/(kind+'.png')
            target=out/'Textures'/name/(kind+'.png')
            target.parent.mkdir(parents=True,exist_ok=True)
            if not target.exists() or target.read_bytes()!=source.read_bytes():
                shutil.copyfile(source,target)
            paths.append(target.relative_to(DATA).as_posix())
        palette.append('surface '+name+' '+' '.join(paths))
    (out/'surfaces.catalog').write_text('\n'.join(palette)+'\n',encoding='utf-8')
    files={p.stem:p for p in RESOURCE.rglob('*.glb') if 'Modules' not in p.parts and
           p.stem not in ['Settlement_Assembly_Example']}
    # Catalog excludes source lineup/library files; only independent runtime GLBs enter the pack.
    aliases={
      'Pilgrim':('Pilgrim',1.15),'Infected':('Weak_HollowThrall',1.),
      'Brute':('Danger_RibBrute',.85),'Crawler':('Weak_AshWeaver',1.1),
      'Pillar':('Post_2m4',1.),'Roots':('Mycelium_Roots',1.),
      'Brazier':('Lantern',1.5),'Shrine':('Shrine_MutantSaint',1.),
      'Potion':('BloodVial',1.8),'Fragment':('Egg_Cluster',.35),
      'BrokenWall':('Wall_Ruined_2m',1.),'Arch':('Arch_Ruined_2m',1.),
      'Statue':('Shrine_MutantSaint',1.3),'Grave':('Rock_Spire',.65),
      'Cart':('Crate_Large',1.),'FungalColony':('Colony_Large',1.),
      'Debris':('Rubble_Cluster',1.),'Corpse':('InfectedWanderer',1.),
      'Fence':('Fence_2m',1.),'DeadTree':('Tree_Dead_A',1.),
      'BlightedTree':('Tree_Infected',1.),'Bedroll':('Bed_Straw',1.),
      'MedicalCot':('Bed_Straw',1.),'WorkTable':('Table',1.),
      'NoticeBoard':('Shelf',1.),'FieldBed':('Grass_Dry_Clump',1.),
      'ResourceWarden':('Warden',1.15),'ResourceHealer':('Healer',1.15),
      'ResourceMerchant':('Scavenger',1.15),'ResourceCivilian':('Body_7H',1.15),
      'ResourceInfected':('InfectedWanderer',1.15),
      'ResourceHound':('Danger_ThornHound',1.),'ResourceSpitter':('Danger_ThornWeaver',.9),
      'ResourceSeeder':('Boss_BroodMatriarch',.7),'ResourceHiveLord':('Boss_CrownedAbomination',.75),
      'ResourceChest':('Chest_Hinged',1.),'ResourceBarrel':('Barrel',1.),
      'ResourceGrass':('Grass_Dry_Clump',1.),'ResourceReeds':('Grass_Reeds',1.),
      'ResourceRock':('Rock_Large',1.),'ResourceEggs':('Egg_Cluster',1.),'ResourceCrate':('Crate_Small',1.)}
    # Resolve authored names explicitly; missing assets fail conversion instead of placeholders.
    available=sorted(files)
    (out/'available_sources.txt').write_text('\n'.join(available)+'\n',encoding='utf-8')
    missing=sorted({name for name,_ in aliases.values()}-files.keys())
    if missing:
        raise ValueError('asset mapping needs exact GLB names: '+', '.join(missing))
    catalog=['version 1']
    hashfile=out/'source_hashes.json'
    prior=json.loads(hashfile.read_text()) if hashfile.exists() else {}
    hashes={}
    profiles=json.loads((RESOURCE/'Documentation/animation_blending.json').read_text(encoding='utf-8'))['profiles']
    info={Path(a['file'].replace('\\','/')).stem:a for a in manifest['assets']}
    for name,path in sorted(files.items()):
        digest=hashlib.sha256(path.read_bytes()+b'rmesh_v2_static_baked_24hz_23float').hexdigest()
        target=out/'Meshes'/(name+'.rmesh')
        if args.force or prior.get(name)!=digest or not target.exists():
            triangles,bones,clips=Glb(path).export(target,materials)
            print(f'{name}: {triangles} triangles, {bones} palette bones, {clips} clips',flush=True)
        hashes[name]=digest
        catalog.append(f'asset {name} Resources/Meshes/{name}.rmesh')
        asset=info.get(name,{})
        profile=profiles.get(asset.get('rig',''),{})
        if 'walk_reference_speed_mps' in profile:
            scale=asset.get('root_scale',1)
            walk=profile['walk_reference_speed_mps'] * scale * (31/30)
            run=profile['run_reference_speed_mps'] * scale * (23/30)
            catalog.append(f'gait {name} {walk:.7f} {run:.7f}')
    for name in ['Post_2m4','Floor_Stone_2m','Floor_Wood_2m','Wall_Stone_2m','Wall_Plaster_2m',
                 'Wall_Doorway_2m','Roof_Slate_2m','Roof_Wood_2m','Gable_Triangle_2m','Chimney',
                 'Stairs_2m','Crate_Small','Table','BloodVial','Lantern']:
        if name not in files:
            raise ValueError('missing prefab construction module '+name)
    order=[]
    for line in (DATA/'models3d.catalog').read_text(encoding='utf-8-sig').splitlines():
        if line.startswith('model '):
            order.append(line.split()[1])
    order += [n for n in aliases if n not in order]
    empty={'ChurchRegalia','RefugeGear','SporeMantle','ResearchGear','PurgeArmor'}
    buildings={'BloodChapel','UnionStorehouse','MyceliumSanctum','BloodObservatory','PurgeBastion'}
    for alias in order:
        scale=aliases.get(alias,('',1.))[1]
        catalog.append(f'prefab {alias} 180 {scale}')
        def part(asset,x=0,y=0,z=0,yaw=0,sx=1,sy=1,sz=1):
            catalog.append(f'part {alias} {asset} {x} {y} {z} {yaw} {sx} {sy} {sz}')
        if alias in empty:
            continue
        if alias in buildings:
            if alias=='MyceliumSanctum':
                part('Colony_Large',sx=1.6,sy=1.6,sz=1.6)
                part('Mycelium_Roots',1,0,0)
                part('Egg_Cluster',-.8,0,.7)
            else:
                part('Floor_Stone_2m',sx=1.5,sz=1.5)
                wall='Wall_Stone_2m' if alias in ['PurgeBastion','BloodChapel'] else 'Wall_Plaster_2m'
                for x,z,yaw in [(0,-1.5,0),(-1.5,0,90),(1.5,0,90)]:
                    part('Wall_Window_2m' if alias=='BloodObservatory' and x else wall,x,0,z,yaw,sx=1.5)
                part('Wall_Doorway_2m',0,0,1.5,sx=1.5)
                for x,yaw in [(-.75,0),(.75,180)]:
                    part('Roof_Slate_2m' if alias!='UnionStorehouse' else 'Roof_Wood_2m',
                         x,2.4,0,yaw,sx=.75,sz=1.5)
                part('Chimney',1,2.1,-.8,sx=.7,sy=.7,sz=.7)
                if alias=='BloodChapel':
                    part('Shrine_MutantSaint',0,0,-.5,sx=.65,sy=.65,sz=.65)
            continue
        if alias not in aliases:
            raise ValueError('unmapped active model alias '+alias)
        part(aliases[alias][0])
        if alias=='Corpse':
            catalog.append('freeze Corpse Death 1')
        if alias=='FieldBed':
            for x,z in [(-.65,-.65),(0,-.65),(.65,-.65),(-.65,0),(.65,0),(-.65,.65),(0,.65),(.65,.65)]:
                part('Grass_Dry_Clump',x,0,z)
        if alias=='WorkTable':
            part('BloodVial',.3,.85,0)
        if alias=='NoticeBoard':
            part('Crate_Small',.8,0,.3)
        if alias=='Cart':
            part('Barrel',.7,0,.2)
            part('Sack',-.6,0,.3)
        if alias=='MedicalCot':
            part('Lantern',.6,0,.2)
    for prop,alias in [('ledger_table','WorkTable'),('ration_board','NoticeBoard'),
                       ('supply_crate','ResourceCrate'),('supply_barrel','ResourceBarrel'),
                       ('meadow_grass','ResourceGrass'),('marsh_reeds','ResourceReeds'),
                       ('hive_eggs','ResourceEggs'),('wild_rock','ResourceRock')]:
        catalog.append(f'bind {prop} {alias}')
    (out/'models.catalog').write_text('\n'.join(catalog)+'\n',encoding='utf-8')
    hashfile.write_text(json.dumps(hashes,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
