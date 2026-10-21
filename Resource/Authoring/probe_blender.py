import bpy, json
from pathlib import Path
p = Path(__file__).resolve().parents[1]
props = bpy.ops.export_scene.gltf.get_rna_type().properties
result = {}
for name in ['export_animation_mode', 'export_nla_strips', 'export_action_filter', 'export_anim_single_armature', 'export_reset_pose_bones', 'export_materials', 'export_shared_accessors']:
    if name in props:
        q = props[name]
        result[name] = [(x.identifier, x.description) for x in q.enum_items] if q.type == 'ENUM' else q.default
(p / 'Authoring/blender_capabilities.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result))
