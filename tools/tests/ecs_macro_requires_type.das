options gen2
options relaxed_assign = false

//! expect: compile-error
//!
//! A component declaration needs an explicit type: `%ecs~("onlyName") %%` must not compile,
//! otherwise a typo in the macro would register a component with no storage.

require tryengine/EcsMacro

%ecs~("onlyName") %%

[export]
def main() {}
