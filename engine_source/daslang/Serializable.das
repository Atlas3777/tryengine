options gen2

module Serializable shared

require daslib/ast_boost
require daslib/decs
require daslib/rtti public
require tryengine/ConfigFormatBoost

//! Сигнатура функции-добавлятора компонента.
typedef ComponentAdder = function<(var cmp : decs::ComponentMap;
                                  component_name : string;
                                  cf : StructField const?) : void>

//! Таблица «имя типа -> функция добавления компонента».
var public g_decs_adders : table<string; ComponentAdder>


def RegisterEntity(var cmp : decs::ComponentMap; component_name : string; cf : StructField const?) {
    var value <- from_CF(cf, default<EntityId>)
    decs::set(cmp, component_name, value)
}

[init]
def RegisterEntityId() {
    g_decs_adders["EntityId"] = @@RegisterEntity
}

//! Универсальная точка входа при загрузке сцены.
//! Возвращает false, если тип не зарегистрирован (не помечен DecsSerializable).
def public DecsAddComponent(var cmp : decs::ComponentMap;
                            type_name : string;
                            component_name : string;
                            cf : StructField const?) : bool {
    if (!g_decs_adders.key_exists(type_name))
        return false
    invoke(g_decs_adders[type_name], cmp, component_name, cf)
    return true
}

[structure_macro(name="DecsSerializable")]
class DecsSerializableMacro : AstStructureAnnotation {
    def override apply(var st : StructurePtr; var group : ModuleGroup;
                       var args : AnnotationArgumentList;
                       var errors : das_string) : bool {

        let type_name = st.name.string()
        let fn_init_name = "register_decs_adder_{type_name}"

        // Сгенерировать init-функцию, которая зарегистрирует лямбду,
        // умеющую превратить CF в $t(st) и положить его в ComponentMap.
        var fnInit <- qmacro_function(fn_init_name) <| $() {
            g_decs_adders[$v(type_name)] = @@(var cmp : decs::ComponentMap;
                                              component_name : string;
                                              cf : StructField const?) : void {
                var value <- from_CF(cf, default<$t(st)>)

                decs::set(cmp, component_name, value)     // <-- runtime param, без $v
            }
        }
        fnInit.flags |= FunctionFlags.init

        if (!(compiling_module() |> add_function(fnInit))) {
            panic("decs_serializable: failed to add {fn_init_name}")
            return false
        }
        return true
    }
}