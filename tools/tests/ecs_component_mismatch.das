options gen2
options relaxed_assign = false

//! expect: exception
//!
//! The declared-type check: `transform` is registered as `Transform`, so creating it with a
//! `float3` must abort entity creation instead of silently landing a wrong-typed component.
//! The runner treats the EXCEPTION as the expected outcome.

require daslib/decs_boost
require tryengine/EcsMacro
require tryengine/Types

[export]
def main() {
    let eid : EntityId = create_entity() @(e : EntityId; var cmp : ComponentMap) {
        cmp |> set("transform", float3(1.0f, 2.0f, 3.0f))
    }
    commit()
    print("FAIL: a wrong component type was accepted (entity {eid.id})\n")
}
