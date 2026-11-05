#pragma once

#include <daScript/daScript.h>

namespace tryengine::core {

inline das::CodeOfPolicies CreateHeavyJitPolicies() {
    das::CodeOfPolicies policies;
    policies.jit_enabled = true;
    policies.jit_dll_mode = true;
    policies.jit_jit_all_functions = true;
    policies.jit_size_level = 0;
    policies.jit_opt_level = 3;
    policies.module_cache = true;
    return policies;
}

inline das::CodeOfPolicies CreatePakPolicies() {
    das::CodeOfPolicies policies;
    policies.jit_enabled = true;
    policies.jit_dll_mode = true;
    policies.jit_jit_all_functions = true;
    policies.jit_size_level = 0;
    policies.jit_opt_level = 3;
    policies.module_cache = true;
    policies.log_compile_time = true;
    policies.log_total_compile_time = true;
    return policies;
}

}  // namespace tryengine::core