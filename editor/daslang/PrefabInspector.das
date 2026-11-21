options gen2
options relaxed_assign = false

module PrefabInspector shared public

// Prefab inspector: disabled on purpose.
//
// The old panel walked the compiled prefab's ConfigFormat tree to draw the node hierarchy,
// and that tree no longer exists: a compiled prefab now holds resolved component values,
// ready to be spawned (`Prefab::PrefabTemplate`). The panel was a quick first pass and its
// interaction model needs a rework on top of the new form, so it draws nothing until then.
// Everything it used to do is still reachable: the Asset Browser loads a selected prefab on
// demand through `Prefab::SpawnPrefab` on drop, and `Prefab::Spawn` is the synchronous
// cache-hit path.

def public DrawPrefabInspector() {
}
