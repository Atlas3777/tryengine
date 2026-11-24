options gen2
options relaxed_assign = false

//! Component registry: flags, schema/writer registration, creators/removers, declared-type
//! checks and archetype validation. Runs against a fresh DECS world, no assets involved.

require daslib/decs_boost
require strings
require tryengine/EcsMacro
require tryengine/ConfigFormatBoost
require tryengine/Serializable
require tryengine/Content
require tryengine/Prefab
require tryengine/TransformSystem
require tryengine/Types

struct ProbeHidden {
    value : int
}
%ecs~("probeHidden", type<ProbeHidden>, hidden = true, transient = true) %%

struct ProbeTransient {
    value : int
}
%ecs~("probeTransient", type<ProbeTransient>, transient = true) %%

[DecsSerializable]
struct ProbeSaved {
    value : int
}
%ecs~("probeSaved", type<ProbeSaved>) %%

[decs_template]
struct ProbeTemplate {
    x : int
}
%ecs_template~(type<ProbeTemplate>, hidden = true, transient = true) %%

var private g_failures : int = 0
var private g_checks : int = 0

def private Check(ok : bool; what : string) {
    g_checks++
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

[export]
def main() : int {
    // --- flags come from the one declaration ---
    Check(EcsShouldSave("transform"), "transform is saved")
    Check(EcsShouldShow("transform"), "transform is shown")
    Check(!EcsShouldSave("_world"), "_world is not saved")
    Check(!EcsShouldShow("_world"), "_world is hidden")
    Check(!EcsShouldSave("relationship"), "relationship is not saved")
    Check(EcsShouldShow("relationship"), "relationship is shown")
    Check(!EcsShouldSave("physicsBody"), "physicsBody is not saved")
    Check(!EcsShouldShow("physicsBody"), "physicsBody is hidden")
    Check(EcsShouldSave("model"), "model is saved")
    Check(EcsShouldSave("tag"), "tag is saved")
    Check(EcsShouldSave("HP"), "HP is saved")
    Check(EcsShouldSave("PointLight_color"), "template field is saved")
    Check(EcsShouldShow("PointLight_intensity"), "template field is shown")
    Check(!EcsShouldSave("nonsense"), "unknown component is not saved")
    Check(!EcsShouldShow("nonsense"), "unknown component is hidden")

    // --- %ecs flags: defaults are saved+shown, flags flip exactly one thing ---
    Check(!EcsShouldShow("probeHidden"), "hidden = true hides")
    Check(!EcsShouldSave("probeHidden"), "hidden = true with transient = true does not save")
    Check(EcsShouldShow("probeTransient"), "transient = true still shows")
    Check(!EcsShouldSave("probeTransient"), "transient = true does not save")
    Check(EcsShouldSave("probeSaved"), "the default is saved")
    Check(EcsShouldShow("probeSaved"), "the default is shown")
    Check(!key_exists(g_ecs_creators, "probeHidden"), "a hidden component is not addable")
    Check(key_exists(g_ecs_creators, "probeSaved"), "a shown component is addable")
    Check(DecsHasInspectSchema("ProbeTransient"), "a shown struct gets an inspector schema")
    Check(!DecsHasInspectSchema("ProbeHidden"), "a hidden struct gets no inspector schema")
    Check(HasComponentWriter("ProbeSaved"), "[DecsSerializable] registered the writer")
    Check(!EcsShouldShow("ProbeTemplate_x"), "a hidden template hides its fields")
    Check(!EcsShouldSave("ProbeTemplate_x"), "a transient template does not save its fields")
    Check(!key_exists(g_ecs_creators, "ProbeTemplate"), "a hidden template is not addable")
    Check(!HasComponentWriter("ProbeTransient"), "a transient-only struct needs no writer")

    // --- the machinery the flags imply was registered ---
    Check(EcsValidateRegistry(), "the registry is consistent")
    Check(KeyExistsWriter("Transform"), "Transform has a writer")
    Check(!HasComponentWriter("WorldTransform"), "WorldTransform has no writer")
    Check(DecsHasInspectSchema("Transform"), "Transform has an inspector schema")
    Check(DecsHasInspectSchema("PointLight"), "PointLight has a group schema")
    Check(!DecsHasInspectSchema("WorldTransform"), "WorldTransform has no schema")
    Check(key_exists(g_ecs_creators, "transform"), "transform can be added")
    Check(key_exists(g_ecs_creators, "PointLight"), "PointLight can be added")
    Check(!key_exists(g_ecs_creators, "PointLight_color"), "a template field is not addable")
    Check(!key_exists(g_ecs_creators, "_world"), "_world is not addable")

    // --- declared types are in DECS before the first archetype ---
    Check(key_exists(decsState.componentTypeCheck, "transform"), "declared type is pushed into DECS")
    Check(key_exists(decsState.componentTypeCheck, "PointLight_color"), "template field type is pushed into DECS")

    // --- a creator from the registry really creates the component ---
    let player : EntityId = create_entity() @(peid : EntityId; var cmp : ComponentMap) {
        cmp |> set("model", ModelRender())
    }
    commit()
    invoke(g_ecs_creators["transform"], player)
    invoke(g_ecs_creators["PointLight"], player)
    var has_transform = false
    query(player) $(transform : Transform) {
        has_transform = true
    }
    var has_light = false
    query(player) $(light : PointLight) {
        has_light = true
    }
    Check(has_transform, "the transform creator added the component")
    Check(has_light, "the PointLight template creator added its fields")
    commit()

    // --- validation accepts the declared archetypes ---
    Check(EcsValidateArchetypes(), "declared archetypes validate")

    // --- an undeclared component is reported ---
    let bogus : EntityId = create_entity() @(beid : EntityId; var cmp : ComponentMap) {
        cmp |> set("bogusComponent", 42)
    }
    commit()
    Check(!EcsValidateArchetypes(), "an undeclared component fails validation")

    // --- a DECS restart drops the types; the registry puts them back ---
    decs::restart()
    Check(!key_exists(decsState.componentTypeCheck, "transform"), "restart clears DECS types")
    EcsApplyRegistry()
    Check(key_exists(decsState.componentTypeCheck, "transform"), "the registry restores DECS types")

    // --- removing through the registry ---
    let eid2 : EntityId = create_entity() @(xeid : EntityId; var cmp : ComponentMap) {}
    commit()
    invoke(g_ecs_creators["relationship"], eid2)
    var rel_before = false
    query(eid2) $(relationship : Relationship) {
        rel_before = true
    }
    invoke(g_ecs_removers["relationship"], eid2)
    var rel_after = false
    query(eid2) $(relationship : Relationship) {
        rel_after = true
    }
    Check(rel_before, "the relationship creator added the component")
    Check(!rel_after, "the relationship remover removed the component")

    // --- the save path writes only declared save = true components ---
    let saved : EntityId = create_entity() @(seid : EntityId; var cmp : ComponentMap) {
        cmp |> set("model", ModelRender(mesh = 0ul, material = ""))
    }
    commit()
    invoke(g_ecs_creators["transform"], saved)
    invoke(g_ecs_creators["relationship"], saved)
    invoke(g_ecs_creators["HP"], saved)
    let text = Prefab::BuildPrefabTextFromEntity(saved, "probe")
    Check(!empty(text), "the prefab text was built")
    Check(find(text, "transform") >= 0, "the prefab keeps transform")
    Check(find(text, "model") >= 0, "the prefab keeps model")
    Check(find(text, "HP") >= 0, "the prefab keeps a declared scalar component")
    Check(find(text, "_world") < 0, "the prefab drops _world")
    Check(find(text, "relationship") < 0, "the prefab drops relationship")

    // --- the load path rejects a not-saved component instead of instantiating it ---
    for (bad_name in ["_world", "relationship"]) {
        let bad = replace(text, "transform", bad_name)
        var doc = Content::ParseFor(DocumentKind.Prefab, "probe.prefab", bad)
        Check(doc != null, "the modified prefab text parses")
        let compiled = Prefab::CompilePrefab(doc)
        Check(compiled == null, "a prefab carrying '{bad_name}' is rejected")
    }

    print("-- {g_failures} of {g_checks} checks failed --\n")
    return g_failures == 0 ? 0 : 1
}

def private KeyExistsWriter(name : string) : bool {
    return HasComponentWriter(name)
}
