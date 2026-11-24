options gen2
options relaxed_assign = false

module EcsMacro shared public

//! One declaration system for ECS components, two shapes:
//!
//!     %ecs~("transform", type<Transform>) %%                        saved and shown
//!     %ecs~("_world", type<WorldTransform>, hidden = true, transient = true) %%
//!     %ecs~("HP", type<uint>) %%
//!     %ecs_template~(type<PointLight>) %%                           a [decs_template] component
//!
//! `%ecs(name, type<T>)` is one value under an explicit name. `%ecs_template(type<T>)` is a
//! [decs_template] component: its fields become `<T>_<field>` components, so the type *is*
//! the name and there is no name argument.
//!
//! `hidden = true` — the Inspector does not draw the component and "Add component" does not
//! list it. `transient = true` — scenes and prefabs neither write nor read it. Both default
//! to false: a declared component is saved and shown unless it says otherwise.
//!
//! Whether a *type* can be written at all is a separate, physical property: a one-value
//! struct component must be marked [DecsSerializable], which registers its ConfigFormat
//! adder and writer. A [decs_template] component is stored field by field, so it needs no
//! annotation; each field type must be writable. This is checked once at EngineStart by
//! `EcsValidateRegistry`.
//!
//! The declared type is pushed into `decsState.componentTypeCheck` at registration, so a
//! component created with a declared name but a different type panics at creation instead of
//! defining the type by first use. Every archetype that appears later is verified once at
//! the start of EngineUpdate (`EcsValidateArchetypes`).

require daslib/ast
require daslib/ast_boost
require daslib/apply
require strings
require daslib/strings_boost
require daslib/decs_boost
require tryengine/Serializable

struct public EcsComponentDesc {
    name      : string
    type_name : string
    //! Component value type, built like DECS builds it, so hashes compare equal.
    info      : CTypeInfo
    //! The component is not written to scenes/prefabs.
    transient : bool
    //! The component is not drawn by the Inspector and not listed in "Add component".
    hidden    : bool
    //! Template group this field belongs to; "" for a single component.
    group     : string
    //! The entry has a creator and is listed in "Add component" (single component or the
    //! group entry of a template; field entries are not addable).
    addable   : bool
}

var public g_ecs_components : table<string; EcsComponentDesc>
var public g_ecs_creators : table<string; lambda<(eid : EntityId) : void>>
var public g_ecs_removers : table<string; lambda<(eid : EntityId) : void>>

// Archetypes are appended and never removed, so this count is the validation cursor.
// `decs::restart` replaces the heap and `EcsApplyRegistry` resets the cursor.
var private g_validated_archetypes : int

//! True when scenes and prefabs store the component `cmp_name`.
def public EcsShouldSave(cmp_name : string) : bool =>
    key_exists(g_ecs_components, cmp_name) ? !(g_ecs_components |> get_value(cmp_name)).transient : false

//! True when the Inspector shows the component `cmp_name`.
def public EcsShouldShow(cmp_name : string) : bool =>
    key_exists(g_ecs_components, cmp_name) ? !(g_ecs_components |> get_value(cmp_name)).hidden : false

//! CTypeInfo of a component value type, built the same way DECS does in `make_component`.
def public EcsTypeInfo(t : type<auto(TT)>) : CTypeInfo {
    unsafe {
        let tinfo = addr(typeinfo rtti_typeinfo(type<TT -const -& -#>))
        return CTypeInfo(
            basicType = tinfo.basicType,
            mangledName = get_mangled_name(tinfo),
            fullName = typeinfo fulltypename(type<TT -const -& -#>),
            hash = tinfo.hash,
            size = tinfo.size
        )
    }
}

//! Declares one component (a single component or one field of a template).
//! The type is pushed into DECS immediately; declaring the same name twice with different
//! types is a programming error and panics here, at init, not on the first query.
def public EcsDeclare(cmp_name : string; t : auto(TT); hidden, transient : bool; group : string; addable : bool) {
    var desc : EcsComponentDesc
    desc.name = cmp_name
    desc.type_name = NormalizeTypeName(typeinfo stripped_typename(type<TT>))
    desc.info = EcsTypeInfo(type<TT>)
    desc.transient = transient
    desc.hidden = hidden
    desc.group = group
    desc.addable = addable
    if (key_exists(decsState.componentTypeCheck, cmp_name)) {
        let prev = decsState.componentTypeCheck |> get_value(cmp_name)
        if (prev.hash != desc.info.hash) {
            panic("ecs: component '{cmp_name}' is declared twice with different types: '{prev.fullName}' and '{desc.info.fullName}'")
        }
    }
    g_ecs_components[cmp_name] = desc
    decsState.componentTypeCheck[cmp_name] = desc.info
}

//! The Inspector field schema of a struct type. It is display data, so it belongs to the
//! component declaration (`%ecs`), not to the save/load annotation. Scalars are drawn by
//! the Inspector as a leaf value and need no schema.
def private RegisterInspectSchema(t : type<auto(TT)>) {
    static_if (typeinfo is_struct(type<TT>)) {
        let schema_key = NormalizeTypeName(typeinfo stripped_typename(type<TT>))
        g_decs_inspect_fields[schema_key] = @@(var out : array<InspectField>) : void {
            CollectInspectFields(type<TT>, out)
        }
    }
}

//! Single component: one value under `cmp_name`. A hidden component has no creator and no
//! remover: the Inspector is the only thing that adds and removes components by hand.
def public EcsRegister(cmp_name : string; t : auto(TT); hidden : bool = false; transient : bool = false) {
    // The type tag is rebuilt instead of forwarded: a `type<T>` result may be passed but
    // never read, and forwarding a tag parameter is a read (lint 30264).
    EcsDeclare(cmp_name, type<TT>, hidden, transient, "", !hidden)
    if (hidden) {
        return
    }
    RegisterInspectSchema(type<TT>)
    g_ecs_creators[cmp_name] <- @(eid : EntityId) {
        update_entity(eid) @(eid, cmp) {
            let deff = default<TT>
            cmp |> set(cmp_name, deff)
        }
        commit()
    }
    g_ecs_removers[cmp_name] <- @(eid : EntityId) {
        update_entity(eid) @(eid, cmp) {
            decs::remove(cmp, cmp_name)
        }
        commit()
    }
}

//! A [decs_template] component: one group the Inspector adds and removes, plus one
//! component per prefixed field (`PointLight_color`, ...). The type *is* the name, because
//! it is also the field prefix `[decs_template]` generates; there is no name argument to
//! disagree with it. Fields are set and removed by that name at compile time through
//! `apply`, so no function generated in the declaring module is needed here. The default
//! prefix is assumed: a `[decs_template(prefix = "...")]` is not supported.
def public EcsRegisterTemplate(t : auto(TT); hidden : bool = false; transient : bool = false) {
    let type_name = NormalizeTypeName(typeinfo stripped_typename(type<TT>))
    EcsDeclare(type_name, type<TT>, hidden, transient, type_name, !hidden)
    var deff = default<TT>
    apply(deff) $(_fname : string; field; _ann) {
        EcsDeclare("{type_name}_{_fname}", type<typedecl(field)>, hidden, transient, type_name, false)
    }
    if (hidden) {
        return
    }
    RegisterInspectSchema(type<TT>)
    g_ecs_creators[type_name] <- @(eid : EntityId) {
        update_entity(eid) @(eid, cmp) {
            apply(default<TT>) $(_fname : string; field; _ann) {
                cmp |> set("{type_name}_{_fname}", field)
            }
        }
        commit()
    }
    g_ecs_removers[type_name] <- @(eid : EntityId) {
        update_entity(eid) @(eid, cmp) {
            apply(default<TT>) $(_fname : string; _field; _ann) {
                decs::remove(cmp, "{type_name}_{_fname}")
            }
        }
        commit()
    }
}

//! Re-pushes the declared types into DECS after `decs::restart` replaced its state.
//! Also resets the per-archetype validation cursor, because the new heap starts empty.
def public EcsApplyRegistry() {
    g_validated_archetypes = 0
    clear(decsState.componentTypeCheck)
    for (name, desc in keys(g_ecs_components), values(g_ecs_components)) {
        decsState.componentTypeCheck[name] = desc.info
    }
}

//! Cross-checks the declarations against the types: a component that is not transient must
//! have a writer for its value. A one-value struct gets one from [DecsSerializable]; a
//! template field gets one from its field type; a scalar has a builtin writer. Runs once
//! from EngineStart, after every [init] has registered its writers.
def public EcsValidateRegistry() : bool {
    var ok = true
    for (name, desc in keys(g_ecs_components), values(g_ecs_components)) {
        // A template group has no value of its own: its fields are what gets stored.
        let is_group = !empty(desc.group) && desc.group == desc.name
        if (!desc.transient && !is_group && !HasComponentWriter(desc.type_name)) {
            print("[ECS ERROR] component '{name}' is not transient but type '{desc.type_name}' has no writer; mark the struct [DecsSerializable] or declare it transient = true\n")
            ok = false
        }
    }
    return ok
}

//! Verifies every archetype that appeared since the last call: each component name must be
//! declared and hold the declared type. Pointer components are runtime references (resolved
//! mesh/material handles) and have no declaration. Returns false when something is wrong;
//! the errors are printed once per archetype, not once per frame.
def public EcsValidateArchetypes() : bool {
    let total = length(decsState.allArchetypes)
    if (total <= g_validated_archetypes) {
        return true
    }
    var ok = true
    for (arch_idx in range(g_validated_archetypes, total)) {
        for (comp in decsState.allArchetypes[arch_idx].components) {
            if (comp.name == "eid" || comp.info.basicType == Type.tPointer) {
                continue
            }
            if (!key_exists(g_ecs_components, comp.name)) {
                print("[ECS ERROR] component '{comp.name}' of type '{NormalizeTypeName(comp.info.fullName)}' is not declared; add %ecs\n")
                ok = false
                continue
            }
            let desc = g_ecs_components |> get_value(comp.name)
            if (desc.type_name != NormalizeTypeName(comp.info.fullName)) {
                print("[ECS ERROR] component '{comp.name}' is declared as '{desc.type_name}', but the archetype holds '{comp.info.fullName}'\n")
                ok = false
            }
        }
    }
    g_validated_archetypes = total
    return ok
}

// --- reader macros: %ecs for a named value, %ecs_template for a type-named template ---

def private ReadComponentSequence(var expr : ExprReader?; ch : int) : bool {
    append(expr.sequence, ch)
    if (ends_with(expr.sequence, "%%")) {
        let len = length(expr.sequence)
        resize(expr.sequence, len - 2)
        return false
    }
    return true
}

def private TrimComponentSequence(expr : ExprReader?) : string {
    var seqStr = string(expr.sequence) |> trim()
    if (starts_with(seqStr, "(") && ends_with(seqStr, ")")) {
        seqStr = trim(slice(seqStr, 1, length(seqStr) - 1))
    }
    return seqStr
}

//! %ecs~("name", type<T>, hidden = ..., transient = ...) %% — one value under `name`.
[reader_macro(name="ecs")]
class EcsComponentReader : AstReaderMacro {
    def override accept(prog : ProgramPtr; mod : Module?; var expr : ExprReader?; ch : int; info : LineInfo) : bool =>
        ReadComponentSequence(expr, ch)

    def override suffix(prog : ProgramPtr; mod : Module?; expr : ExprReader?; info : LineInfo; var outLine : int&; var outFile : FileInfo?&) : string {
        let seqStr = TrimComponentSequence(expr)

        // Split the name from the type and flags at the first comma outside < > [ ] ( ).
        var comma_idx = -1
        var depth = 0
        peek_data(seqStr) $(data) {
            for (i in range(length(data))) {
                let ch = int(data[i])
                if (ch == '<' || ch == '[' || ch == '(') {
                    depth++
                } elif (ch == '>' || ch == ']' || ch == ')') {
                    depth--
                } elif (ch == ',' && depth == 0) {
                    comma_idx = i
                    break
                }
            }
        }

        if (comma_idx == -1) {
            // Escaped braces, since the generated text is a format string
            return "[init]\ndef _ecs_error_{info.line}() \{ panic(\"Invalid %ecs syntax. Use: \\\"Name\\\", type<T>\"); \}\n"
        }

        let name_part = trim(slice(seqStr, 0, comma_idx))
        let tail = trim(slice(seqStr, comma_idx + 1))
        return "[init]\n def _auto_ecs_reg_{info.line}() \{\n EcsRegister({name_part}, {tail});\n\}\n"
    }
}

//! %ecs_template~(type<T>, hidden = ..., transient = ...) %% — a [decs_template] component.
//! The type is the component name: it is the prefix `[decs_template]` gives every field.
[reader_macro(name="ecs_template")]
class EcsTemplateReader : AstReaderMacro {
    def override accept(prog : ProgramPtr; mod : Module?; var expr : ExprReader?; ch : int; info : LineInfo) : bool =>
        ReadComponentSequence(expr, ch)

    def override suffix(prog : ProgramPtr; mod : Module?; expr : ExprReader?; info : LineInfo; var outLine : int&; var outFile : FileInfo?&) : string {
        let seqStr = TrimComponentSequence(expr)
        if (!starts_with(seqStr, "type<")) {
            return "[init]\ndef _ecs_template_error_{info.line}() \{ panic(\"Invalid %ecs_template syntax. Use: type<T>, hidden = ..., transient = ...\"); \}\n"
        }
        return "[init]\n def _auto_ecs_template_reg_{info.line}() \{\n EcsRegisterTemplate({seqStr});\n\}\n"
    }
}
