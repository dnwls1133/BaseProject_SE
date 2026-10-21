import sys, json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import build_resources as C
import build_environment
C.make_materials()
C.MANIFEST=json.loads((C.ROOT/'Documentation/asset_manifest.json').read_text(encoding='utf-8'))
build_environment.build(C)
C.finish_manifest()
print('RESOURCE_BUILD_COMPLETE',flush=True)
