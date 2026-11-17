options gen2
options relaxed_assign = false

module Profiling shared public

//! Script-side instrumentation that feeds the same zones as the C++ profiler
//! (engine/core/Profiler.hpp).
//!
//! Function annotation:
//!
//!     [profile]
//!     def UpdateEnemies() { ... }
//!
//!     [profile(name = "AI: path find")]
//!     def FindPath() { ... }
//!
//! The default name is the function name. The zone is closed through `defer`, so an early
//! `return` still records it.
//!
//! Compile-time removal: `ProfileIsEnabled()` is the C++ binding that reports the
//! TRY_PROFILE_ENABLED value of the binary the engine was built into. When profiling is
//! disabled, `apply` leaves the function body untouched: not a single call remains in the
//! script, exactly like TRY_PROFILE_SCOPE expands to nothing in C++.
//!
//! For an arbitrary code span there is the manual pair: `let t = ProfileBegin("x")` ...
//! `ProfileEnd(t)`. Forgetting the end call is safe — the zone simply never reaches a frame.

require daslib/ast
require daslib/ast_boost
require daslib/templates_boost
require daslib/defer public
require tryRenderer public

/// Zone name: the annotation argument when present, the function name otherwise.
def private ProfileName(args : AnnotationArgumentList; default_name : string) : string {
    let arg = args |> find_arg("name")
    if (arg is tString) {
        return arg as tString
    }
    return default_name
}

[function_macro(name = "profile")]
class ProfileFunctionMacro : AstFunctionAnnotation {
    def override apply(var func : FunctionPtr; var group : ModuleGroup; var args : AnnotationArgumentList;
                       var errors : das_string) : bool {
        if (!ProfileIsEnabled()) {
            return false
        }
        let profile_name = ProfileName(args, string(func.name))
        func.body = qmacro_block() {
            let _profile_token = ProfileBegin($v(profile_name))
            defer() {
                ProfileEnd(_profile_token)
            }
            $e(func.body)
        }
        return true
    }
}
