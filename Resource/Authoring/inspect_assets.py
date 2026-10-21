"""Read delivered GLB payloads; record asset integrity, not game-runtime validation."""
import json, struct, math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def load(path):
    raw=path.read_bytes()
    magic,version,length=struct.unpack_from('<III',raw)
    assert magic==0x46546c67 and version==2 and length==len(raw), str(path)
    jl,jtype=struct.unpack_from('<II',raw,12)
    data=json.loads(raw[20:20+jl])
    binary=b''
    offset=20+jl
    if offset<len(raw):
        n,kind=struct.unpack_from('<II',raw,offset)
        binary=raw[offset+8:offset+8+n]
    return data,binary

def accessor(g,b,index):
    a=g['accessors'][index]
    v=g['bufferViews'][a['bufferView']]
    code={5120:'b',5121:'B',5122:'h',5123:'H',5125:'I',5126:'f'}[a['componentType']]
    width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
    fmt='<'+code*width
    stride=v.get('byteStride',struct.calcsize(fmt))
    start=v.get('byteOffset',0)+a.get('byteOffset',0)
    return [struct.unpack_from(fmt,b,start+i*stride) for i in range(a['count'])]

report={'files':[],'errors':[]}
manifest=json.loads((ROOT/'Documentation/asset_manifest.json').read_text(encoding='utf-8'))
for path in sorted(ROOT.rglob('*.glb')):
    try:
        g,b=load(path)
        tris=0
        weighted=0
        for mesh in g.get('meshes',[]):
            for p in mesh['primitives']:
                assert p.get('mode',4)==4,'non triangle primitive'
                tris+=g['accessors'][p['indices']]['count']//3
                if 'WEIGHTS_0' in p['attributes']:
                    w=accessor(g,b,p['attributes']['WEIGHTS_0'])
                    assert all(abs(sum(v)-1)<.002 for v in w),'non-normalized skin weights'
                    weighted+=len(w)
        animations=[]
        for a in g.get('animations',[]):
            times=[accessor(g,b,s['input']) for s in a['samplers']]
            assert all(all(math.isfinite(t[0]) for t in track) for track in times)
            animations.append({'name':a['name'],'channels':len(a['channels']),
                'duration_s':max(t[-1][0] for t in times)-min(t[0][0] for t in times)})
        rec={'file':path.relative_to(ROOT).as_posix(),'triangles':tris,'skins':len(g.get('skins',[])),
             'weighted_vertices':weighted,'animations':animations,'bytes':path.stat().st_size}
        report['files'].append(rec)
        if 'Presets' in path.parts or (path.parts[-3]=='Creatures' and path.parent.name in ['Animal','Spider','Giant']):
            assert tris<10000,'triangle budget'
            assert rec['skins']>=1,'missing skeleton'
            assert len(animations)>=12,'missing animation clips'
    except Exception as e:
        report['errors'].append({'file':str(path.relative_to(ROOT)),'error':str(e)})
report['summary']={'glb_files':len(report['files']), 'native_files':len(list(ROOT.rglob('*.blend'))),
                   'png_maps':len(list((ROOT/'Materials').rglob('*.png'))),
                   'max_character_triangles':max(a['triangles'] for a in manifest['assets'] if a['category'] in ['character_preset','creature'])}
(ROOT/'Documentation/asset_integrity.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({'summary':report['summary'],'errors':report['errors']},ensure_ascii=False))
for r in report['files']:
    if '/Presets/' in r['file'] or (r['file'].startswith('Creatures/') and '/Modules/' not in r['file']):
        print(r['file'],r['triangles'],r['skins'],[a['name'] for a in r['animations']])
