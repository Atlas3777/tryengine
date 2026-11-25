options gen2
options no_unused_block_arguments = false
options no_unused_function_arguments = false
options strict_smart_pointers = true

module Live shared public

//! `@live` globals and struct fields that survive a script generation swap.
//!
//! The pass is modelled on `live/live_vars` from dasLiveHost, but the store belongs to this
//! engine: `live_store_bytes` / `live_load_bytes` are native `tryLive` functions backed by
//! `engine/core/LiveStore.hpp`. The host calls the generated hooks around the context swap:
//! `[before_reload]` in the old generation writes the values, and `[after_reload]` in the new
//! one reads them back.
//!
//!     require tryengine/Live
//!
//!     var @live counter = 0
//!     var @live values : array<float>
//!
//! Every target is stored under its own key built from the module and variable names. The
//! data carries a hash of the source initializer: changing the default in code discards the
//! stored value instead of restoring a stale one.
//!
//! `@live` also works on struct fields. A global whose struct has `@live` fields is preserved
//! field by field; the fields without the annotation take their source defaults on reload.
//! `@live` on a `let` (or on a constant field) is a compile error: a constant has nothing to
//! restore.

require daslib/ast
require daslib/rtti
require daslib/ast_boost
require daslib/templates_boost
require daslib/archive // Used by the generated blocks only; the pass adds the require to the target module.
require tryLive public

//! True when the running generation came from a hot reload, not from the process start.
def is_reload() : bool {
    return live_is_reload()
}

//! `@live` on a constant is a mistake, and it would also crash the save hook trying to write
//! a read-only value. Report it at compile time, once per declaration.
[lint_macro]
class LiveLint : AstPassMacro {
    def override apply(prog : ProgramPtr; mod : Module?) : bool {
        if (mod.name == "Live") {
            return false
        }
        mod |> for_each_global() $(g) {
            if (g.annotation |> find_arg("live") ?as tBool ?? false) {
                if (g._type.flags.constant) {
                    macro_error(prog, g.at, "@live cannot be used on constant (let) variables - they never change: {g.name}")
                }
            }
        }
        for_each_structure(mod) $(st) {
            for (f in st.fields) {
                if (f.annotation |> find_arg("live") ?as tBool ?? false) {
                    if (f._type.flags.constant) {
                        macro_error(prog, f.at, "@live cannot be used on constant fields: {st.name}.{f.name}")
                    }
                }
            }
        }
        return false
    }
}

struct private LiveVarTarget {
    name : string
    key : string
    init_hash : int64
}

struct private LiveFieldTarget {
    var_name : string
    field_name : string
    key : string
    init_hash : int64
}

//! Generates the two reload hooks for the module that uses `@live`. The hooks are exported
//! and named with the prefixes ScriptSystem discovers; every module gets its own pair.
[infer_macro]
class LivePass : AstPassMacro {
    def override apply(prog : ProgramPtr; mod : Module?) : bool {
        if (mod.name == "Live") {
            return false
        }
        // One pass per module: the compiler may run the infer phase again.
        var found = false
        mod |> for_each_function("__before_reload_live_vars") $(_fn) {
            found = true
        }
        if (found) {
            return false
        }

        // A whole `@live` variable covers all of its fields; field targets are collected only
        // for globals that are not `@live` themselves.
        var var_targets : array<LiveVarTarget>
        var field_targets : array<LiveFieldTarget>

        mod |> for_each_global() $(g) {
            if (g._type.flags.constant) {
                return
            }
            let var_live = g.annotation |> find_arg("live") ?as tBool ?? false
            if (var_live) {
                let init_hash = g.init != null ? int64(hash(describe(g.init))) : int64(0)
                var_targets |> emplace(LiveVarTarget(name = string(g.name), key = "__live_vars_{mod.name}_{g.name}",
                                                     init_hash = init_hash))
            } else {
                let st = g._type.structType
                if (st == null) {
                    return
                }
                // The hash combines the global initializer with the field default: changing
                // either source expression invalidates the stored field value.
                let global_hash = g.init != null ? int64(hash(describe(g.init))) : int64(0)
                for (f in st.fields) {
                    if (f.annotation |> find_arg("live") ?as tBool ?? false) {
                        if (f._type.flags.constant) {
                            continue
                        }
                        let field_hash = f.init != null ? int64(hash(describe(f.init))) : int64(0)
                        field_targets |> emplace(LiveFieldTarget(
                            var_name = string(g.name), field_name = string(f.name),
                            key = "__live_vars_{mod.name}_{g.name}__{f.name}", init_hash = global_hash ^ field_hash))
                    }
                }
            }
        }
        if (empty(var_targets) && empty(field_targets)) {
            return false
        }

        // The generated blocks construct Archive and MemSerializer, so the target module has
        // to see the archive module even when the author never required it.
        var archive_module : Module?
        program_for_each_module(prog) $(m) {
            if (m.name == "archive") {
                archive_module = m
            }
        }
        if (archive_module != null) {
            add_module_require(mod, archive_module, false)
        }

        let total_targets = length(var_targets) + length(field_targets)

        var save_blocks : array<ExpressionPtr>
        save_blocks |> reserve(total_targets)
        for (t in var_targets) {
            save_blocks |> push <| qmacro_block() {
                var ser = new MemSerializer()
                var arch = Archive(reading = false, stream = ser)
                var init_hash = $v(t.init_hash)
                arch |> _::serialize(init_hash)
                arch |> _::serialize($i(t.name))
                let data <- ser->extractData()
                live_store_bytes($v(t.key), data)
                unsafe { delete ser; }
            }
        }
        for (t in field_targets) {
            save_blocks |> push <| qmacro_block() {
                var ser = new MemSerializer()
                var arch = Archive(reading = false, stream = ser)
                var init_hash = $v(t.init_hash)
                arch |> _::serialize(init_hash)
                arch |> _::serialize($i(t.var_name).$f(t.field_name))
                let data <- ser->extractData()
                live_store_bytes($v(t.key), data)
                unsafe { delete ser; }
            }
        }

        var save_fn = qmacro_function("__before_reload_live_vars") $() {
            $b(save_blocks)
        }
        save_fn.flags |= FunctionFlags.exports | FunctionFlags.generated
        compiling_module() |> add_function(save_fn)

        var load_blocks : array<ExpressionPtr>
        load_blocks |> reserve(total_targets)
        for (t in var_targets) {
            load_blocks |> push <| qmacro_block() {
                var data : array<uint8>
                if (live_load_bytes($v(t.key), data) && length(data) > 0) {
                    var ser = new MemSerializer(data)
                    var arch = Archive(reading = true, stream = ser)
                    var stored_hash : int64
                    arch |> _::serialize(stored_hash)
                    if (stored_hash == $v(t.init_hash)) {
                        arch |> _::serialize($i(t.name))
                    }
                    unsafe { delete ser; }
                }
            }
        }
        for (t in field_targets) {
            load_blocks |> push <| qmacro_block() {
                var data : array<uint8>
                if (live_load_bytes($v(t.key), data) && length(data) > 0) {
                    var ser = new MemSerializer(data)
                    var arch = Archive(reading = true, stream = ser)
                    var stored_hash : int64
                    arch |> _::serialize(stored_hash)
                    if (stored_hash == $v(t.init_hash)) {
                        arch |> _::serialize($i(t.var_name).$f(t.field_name))
                    }
                    unsafe { delete ser; }
                }
            }
        }

        var load_fn = qmacro_function("__after_reload_live_vars") $() {
            $b(load_blocks)
        }
        load_fn.flags |= FunctionFlags.exports | FunctionFlags.generated
        compiling_module() |> add_function(load_fn)
        return true
    }
}
