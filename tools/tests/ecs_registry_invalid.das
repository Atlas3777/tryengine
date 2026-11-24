options gen2
options relaxed_assign = false

//! A component saved by default whose type has no [DecsSerializable] writer is the mistake the
//! registry validation has to report. The declaration below makes the registry invalid on
//! purpose, so `EcsValidateRegistry` must return false and print the reason.

require daslib/decs_boost
require tryengine/EcsMacro

struct NoWriter {
    value : int
}
%ecs~("noWriter", type<NoWriter>) %%

[export]
def main() : int {
    if (EcsValidateRegistry()) {
        print("FAIL: the registry accepted a non-transient component without a writer\n")
        return 1
    }
    print("-- 0 failures --\n")
    return 0
}
