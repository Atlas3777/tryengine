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

// --- Обычный одиночный компонент (%ecs~("HP", type<uint>)%%) ---

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

// --- [EcsTemplate]: генерирует регистрацию шаблона прямо в модуле пользователя ---

[structure_macro(name="EcsTemplate")]
class EcsTemplateMacro : AstStructureAnnotation {
    def override apply(var st : StructurePtr; var group : ModuleGroup;
                       var args : AnnotationArgumentList; var errors : das_string) : bool {
        let type_name = st.name.string()
        // Тот же дефолтный префикс, что у [decs_template].
        // Если кто-то ставит [decs_template(prefix = "...")] — надо дублировать
        // тот же prefix и здесь (см. примечание ниже).
        let prefix = "{st.name}_"

        // Для removal собираем набор decs::remove(cmp, "<prefix><field>")
        // по всем полям шаблона — это ровно то, что делает remove_decs_template`T,
        // но без backtick-имени, чтобы спокойно пройти через qmacro.
        var removeBlk : array<ExpressionPtr>
        for (fld in st.fields) {
            let fname = "{prefix}{fld.name}"
            removeBlk |> push(qmacro(decs::remove(cmp, $v(fname))))
        }

        // Регистрация:
        //   - creator: apply_decs_template(cmp, default<T>) — раскладывает поля с префиксом
        //   - remover: прямые decs::remove по каждому префиксному имени
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

// --- reader macro %ecs~…%% — без изменений ---

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

        // Убираем внешние скобки, если они есть: ("HP", type<uint>) -> "HP", type<uint>
        if (starts_with(seqStr, "(") && ends_with(seqStr, ")")) {
            seqStr = trim(slice(seqStr, 1, length(seqStr) - 1))
        }

        var depth = 0
        var comma_idx = -1

        // Ищем запятую, игнорируя те, что внутри < >, [ ], ( )
        // peek_data открывает строку как array<uint8>, что позволяет безопасно и быстро читать байты по индексу
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
            // Используем \{ и \} вместо {{ и }}
            return "[init]\ndef _ecs_error_{info.line}() \{ panic(\"Invalid %%ecs syntax. Use: \\\"Name\\\", Type\"); \}\n"
        }

        // Разделяем по найденной запятой на нулевом уровне вложенности
        let name_part = trim(slice(seqStr, 0, comma_idx))
        let type_part = trim(slice(seqStr, comma_idx + 1))

        // Генерируем валидный top-level код (также исправляем скобки)
        let code = "[init]\n def _auto_ecs_reg_{info.line}() \{\n EcsRegister({name_part}, {type_part});\n\}\n"
        return code
    }
}