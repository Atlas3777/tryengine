options gen2
options relaxed_assign = false

module EcsMacro shared public

require daslib/ast
require daslib/ast_boost
require strings
require daslib/strings_boost
require daslib/decs_boost
require daslib/templates_boost

struct EcsCompName{
    cmp_type    : string
    cmp_creator : lambda<(eid : EntityId) : void>
    cmp_remover : lambda<(eid : EntityId) : void>
}

var ecs_component_table : table<string, EcsCompName>

// --- Single component: EcsRegister from `%ecs~("HP", type<uint>) %%` or from [EcsComponent] ---

def public EcsRegister(cmp_name : string; t : auto(TT)) {
    var a : EcsCompName
    a.cmp_type = typeinfo stripped_typename(type<TT>)
    a.cmp_creator <- @(eid : EntityId){
        update_entity(eid) @(eid, cmp) {
            var deff = default<TT>
            cmp |> set(cmp_name, deff)
        }
        commit()
    }
    a.cmp_remover <- @(eid : EntityId){
        update_entity(eid) @(eid, cmp) {
            decs::remove(cmp, cmp_name)
        }
        commit()
    }
    ecs_component_table[cmp_name] <- a
}

// --- [EcsComponent(name = "transform")]: a struct that is one component ---
//
// [EcsTemplate] only fits a [decs_template] struct: its creator expands the struct into
// prefixed fields with apply_decs_template. A struct that DECS stores as one value is
// registered here instead, so the component name lives on the type declaration.

[structure_macro(name="EcsComponent")]
class EcsComponentMacro : AstStructureAnnotation {
    def override apply(var st : StructurePtr; var group : ModuleGroup;
                       var args : AnnotationArgumentList; var errors : das_string) : bool {
        let name_arg = args |> find_arg("name")
        if (!(name_arg is tString)) {
            errors := "EcsComponent: name must be a string, e.g. [EcsComponent(name = \"transform\")]"
            return false
        }
        let cmp_name = name_arg as tString
        if (empty(cmp_name)) {
            errors := "EcsComponent: name must not be empty"
            return false
        }
        let type_name = st.name.string()
        var fnReg <- qmacro_function("ecs_register_{type_name}") $() : void {
            EcsRegister($v(cmp_name), type<$t(st)>)
        }
        fnReg.flags.init = true
        if (!(compiling_module() |> add_function(fnReg))) {
            errors := "EcsComponent: can't add registration for {type_name}"
            return false
        }
        return true
    }
}

// --- [EcsTemplate]: registers a template in the module that declares it ---

[structure_macro(name="EcsTemplate")]
class EcsTemplateMacro : AstStructureAnnotation {
    def override apply(var st : StructurePtr; var group : ModuleGroup;
                       var args : AnnotationArgumentList; var errors : das_string) : bool {
        let type_name = st.name.string()
        // The same default prefix as [decs_template].
        // A [decs_template(prefix = "...")] must repeat the same prefix here.
        let prefix = "{st.name}_"

        // Removal collects decs::remove(cmp, "<prefix><field>") for every field --
        // the same as remove_decs_template`T, but without the backtick name so it
        // survives qmacro.
        var removeBlk : array<ExpressionPtr>
        for (fld in st.fields) {
            let fname = "{prefix}{fld.name}"
            removeBlk |> push(qmacro(decs::remove(cmp, $v(fname))))
        }

        // Registration:
        //   - creator: apply_decs_template(cmp, default<T>) expands the prefixed fields
        //   - remover: direct decs::remove for every prefixed field name
        var fnReg = qmacro_function("ecs_register_template_{type_name}") $() : void {
            var a : EcsCompName
            a.cmp_type = $v(type_name)
            a.cmp_creator <- @(eid : EntityId) {
                update_entity(eid) @(eid, cmp) {
                    var deff = default<$t(st)>
                    apply_decs_template(cmp, deff)
                }
                commit()
            }
            a.cmp_remover <- @(eid : EntityId) {
                update_entity(eid) @(eid, cmp) {
                    $b(removeBlk)
                }
                commit()
            }
            ecs_component_table[$v(type_name)] <- a
        }
        fnReg.flags |= FunctionFlags.init

        if (!(compiling_module() |> add_function(fnReg))) {
            errors := "EcsTemplate: can't add registration for {type_name}"
            return false
        }
        return true
    }
}

// --- reader macro %ecs~...%%: registers a builtin scalar component ---

[reader_macro(name="ecs")]
class EcsComponentReader : AstReaderMacro {
    def override accept(prog : ProgramPtr; mod : Module?; var expr : ExprReader?; ch : int; info : LineInfo) : bool {
        append(expr.sequence, ch)
        if (ends_with(expr.sequence, "%%")) {
            let len = length(expr.sequence)
            resize(expr.sequence, len - 2)
            return false
        }
        return true
    }

    def override suffix(prog : ProgramPtr; mod : Module?; expr : ExprReader?; info : LineInfo; var outLine : int&; var outFile : FileInfo?&) : string {
        var seqStr = string(expr.sequence) |> trim()

        // Strip the outer parentheses when present: ("HP", type<uint>) -> "HP", type<uint>
        if (starts_with(seqStr, "(") && ends_with(seqStr, ")")) {
            seqStr = trim(slice(seqStr, 1, length(seqStr) - 1))
        }

        var depth = 0
        var comma_idx = -1

        // Find the comma that is not nested inside < >, [ ] or ( ).
        // peek_data exposes the string as array<uint8> for indexed byte access.
        if (!empty(seqStr)) {
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
        }

        if (comma_idx == -1) {
            // Escaped braces, since the generated text is a format string
            return "[init]\ndef _ecs_error_{info.line}() \{ panic(\"Invalid %%ecs syntax. Use: \\\"Name\\\", Type\"); \}\n"
        }

        // Split at the top-level comma
        let name_part = trim(slice(seqStr, 0, comma_idx))
        let type_part = trim(slice(seqStr, comma_idx + 1))

        // Emit top-level code that registers the component
        let code = "[init]\n def _auto_ecs_reg_{info.line}() \{\n EcsRegister({name_part}, {type_part});\n\}\n"
        return code
    }
}