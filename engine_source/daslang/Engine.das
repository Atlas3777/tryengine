options gen2
options relaxed_assign = false
options gc = true
options persistent_heap
options intern_strings

module Engine shared public

require tryengine/AsyncTaskManager
require tryengine/EcsMacro
require tryengine/Input
require tryengine/Profiling
require daslib/just_in_time
require tryengine/Physics
require tryengine/Time
require tryengine/TransformSystem
require daslib/decs
// A zone around the tick: tick_async_tasks finishes hot-reload work for scripts, and one
// frame in a long run can cost hundreds of milliseconds. Without a name that time hangs on
// the frame root and reads as "no idea where it went".
[export, profile]
def EngineUpdate() {
    // Archetypes created since the last frame (scene load, prefab spawn, editor edit) are
    // checked against the component declarations before any system reads them.
    EcsValidateArchetypes()
    // The frame's dt is read here and nowhere else; every clock derives from it, so all
    // systems see the same frame and no caller passes dt around.
    TimeTick()
    tick_async_tasks()
    Physics::Update()
    UpdateWorldTransforms()
}

//! Garbage-collection boundary for a host whose program declares `options gc`.
//! DECS keeps component strings and arrays in raw archetype memory, so the collector can
//! see them only through the temporary `gc_dummy` lambdas that this function sets.
//! Call this from the frame loop between script calls, never from inside a query. A host
//! without a collectable heap (the game client) must not call it.
//!
//! Only components that actually own das-managed values need a root: strings, arrays,
//! tables and structs with a GC field. Rooting a POD component (Transform, PhysicsBody,
//! colliders) is pointless and the generic walker is not safe for every such layout;
//! pointer components (`mesh`/`material`) address C++ resources and must stay unrooted.
def private NeedsGcRoot(comp : Component) : bool {
    let bt = comp.info.basicType
    if (bt == Type.tString || bt == Type.tArray || bt == Type.tTable ||
        bt == Type.tTuple || bt == Type.tVariant || bt == Type.tLambda) {
        return true
    }
    if (bt != Type.tStructure) {
        return false
    }
    let ti = invoke(comp.info.mkTypeInfo)
    if (ti == null || ti.firstType == null || ti.firstType.structType == null) {
        return false
    }
    let st = ti.firstType.structType
    return st.firstGcField < st.count
}

def private MarkGcRoots() {
    for (arch in decsState.allArchetypes) {
        if (arch.size > 0_l) {
            for (comp in arch.components) {
                if (comp.info.gc == null || !NeedsGcRoot(comp)) {
                    continue
                }
                comp.gc_dummy <- invoke(comp.info.gc, comp.data)
            }
        }
    }
}

[export, no_jit]
def EngineCollectGarbage() {
    MarkGcRoots()
    unsafe { heap_collect_if_needed() }
    decs::after_gc()
}

[export]
def EngineStart() {
    TimeInit()
    SetInputSource()
    // Every module's [init] has run by now, so the component registrations are complete.
    // A declaration that asks for save/show must have the machinery it implies; report it
    // at start, not on the first save or the first Inspector frame.
    EcsApplyRegistry()
    EcsValidateRegistry()
}
