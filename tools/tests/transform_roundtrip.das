options gen2
options relaxed_assign = false

require daslib/decs
require daslib/decs_boost
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content
require tryengine/Serializable
require tryengine/TransformSystem
require tryengine/Types

//! The one-component `transform` survives both directions of the scene format — a document
//! field applied to a ComponentMap, and a component written back to ConfigFormat.
let kSceneText = "entities: Entity[] = [\n\t\{\n\t\ttransform: Transform = \{\n\t\t\tposition: f32x3 = [1 2 3]\n\t\t\tscale: f32x3 = [2 2 2]\n\t\t\}\n\t\ttag: string = \"x\"\n\t\}\n]\n"

[export]
def main() : int {
    var ok = true

    // Document -> ComponentMap, like Scene::Load does it.
    let document = Content::Parse("content/roundtrip.scene", kSceneText, Content::ItemsField(DocumentKind.Scene))
    if (document == null) {
        print("FAIL: scene text did not parse\n")
        return 1
    }
    assume root_obj = document.root.value as _object
    let entities_cf = root_obj?["entities"] ?? null
    assume entities_arr = entities_cf.value as _array
    let first_entity = entities_arr[0]
    assume entity_obj = first_entity.value as _object

    var cmp : ComponentMap
    for (field_name in keys(entity_obj)) {
        let cf = entity_obj?[field_name] ?? null
        if (!ApplyDecsConfigField(cmp, field_name, cf, cf != null ? cf.typeName : "")) {
            print("FAIL: field '{field_name}' did not apply\n")
            ok = false
        }
    }
    let loaded = decs::get(cmp, TRANSFORM_FIELD, Transform())
    print("loaded: position={loaded.position} rotation={loaded.rotation} scale={loaded.scale}\n")
    ok = ok && loaded.position == float3(1.0f, 2.0f, 3.0f)
    ok = ok && loaded.rotation == float4(0.0f, 0.0f, 0.0f, 1.0f)
    ok = ok && loaded.scale == float3(2.0f, 2.0f, 2.0f)

    // Component -> ConfigFormat, like scene/prefab save does it.
    decs::restart()
    var eid : EntityId
    create_entity() @(e : EntityId; var created : ComponentMap) {
        eid = e
        ApplyEntityTransform(created, Transform(position = float3(4.0f, 5.0f, 6.0f), scale = float3(3.0f)))
    }
    commit()

    let record = decsState.entityLookup[int(eid.id)]
    let arch_index = int(decsState.archetypeLookup[record.archetype]) - 1
    let component_index = decs::find_component_index(decsState.allArchetypes[arch_index], TRANSFORM_FIELD)
    var saved = DecsComponentToCF(decsState.allArchetypes[arch_index].components[component_index], record.index)
    if (saved == null) {
        print("FAIL: transform component is not serializable\n")
        return 1
    }
    saved.name = TRANSFORM_FIELD
    var save_root : table<string; StructField?>
    save_root[TRANSFORM_FIELD] = saved
    let saved_text = write_config(CF(save_root))
    print("saved:\n{saved_text}\n")
    ok = ok && saved.typeName == "Transform"
    ok = ok && !empty(saved_text)

    // ...and the written text applies back.
    let parsed_doc = Content::Parse("content/roundtrip2.scene", saved_text, Content::ItemsField(DocumentKind.Scene))
    if (parsed_doc == null) {
        print("FAIL: saved text does not parse back\n")
        return 1
    }
    assume parsed_obj = parsed_doc.root.value as _object
    let field_cf = parsed_obj?[TRANSFORM_FIELD] ?? null
    var cmp2 : ComponentMap
    if (field_cf == null || !ApplyDecsConfigField(cmp2, TRANSFORM_FIELD, field_cf, field_cf.typeName)) {
        print("FAIL: written transform did not apply back\n")
        return 1
    }
    let reloaded = decs::get(cmp2, TRANSFORM_FIELD, Transform())
    print("reloaded: position={reloaded.position} scale={reloaded.scale}\n")
    ok = ok && reloaded.position == float3(4.0f, 5.0f, 6.0f)
    ok = ok && reloaded.scale == float3(3.0f, 3.0f, 3.0f)

    if (!ok) {
        print("FAIL: transform did not round-trip through ConfigFormat\n")
        return 1
    }
    print("-- 0 failures --\n")
    return 0
}
