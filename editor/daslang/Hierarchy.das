options gen2

module Hierarchy

require tryengine/Types

require daslib/decs
require daslib/decs_boost


def public DebugPrint(){
    for (arch in decsState.allArchetypes){
        for (i in range(int(arch.size))) {
            unsafe {
                let eid = get_ro(arch, "eid", EntityId())[i]
                print("Entity ID: {eid}\n")
                print_entity_components_at(arch, i, eid)
            }
        }
    }
}

def print_entity_components_at(arch : Archetype const&; entityIndex : int; eid : EntityId) {
    print("Entity {eid} index {entityIndex} in archetype (size {arch.size})\n")
    for (comp in arch.components) {
        if (comp.name == "eid") { continue }
        unsafe {
            let offset = entityIndex * comp.stride
            let dataPtr = reinterpret<void? const>(addr(comp.data[offset]))
            var valueStr = "???"
            if (comp.info.dumper != null) {
                valueStr = invoke(comp.info.dumper, dataPtr)
            }
            print("  {comp.name} = {valueStr}\n")
        }
    }
}
