options gen2

module Serializable shared

require daslib/ast_boost
require daslib/decs
require daslib/rtti public
require daslib/apply
require daslib/strings_boost
require math
require tryengine/ConfigFormatBoost

//! Сигнатура функции-добавлятора компонента.
typedef ComponentAdder = function<(var cmp : decs::ComponentMap;
                                  component_name : string;
                                  cf : StructField? const) : void>

//! Таблица «имя типа -> функция добавления компонента».
var public g_decs_adders : table<string; ComponentAdder>

//! Сигнатура функции-записывателя компонента: ptr указывает на значение в архетипе DECS.
typedef ComponentWriter = function<(ptr : void?) : StructField?>

//! Таблица «имя типа -> функция записи компонента в ConfigFormat».
var public g_decs_writers : table<string; ComponentWriter>

//! Описание поля структуры, собранное на этапе компиляции через apply.
//! offset считается от начала значения; сам указатель инспектор вычисляет из данных архетипа.
struct public InspectField {
    name        : string
    typeName    : string
    offset      : uint64
    annotations : array<tuple<name : string; data : RttiValue>>
}

//! Сигнатура генератора описаний полей.
typedef InspectFieldsFn = function<(var out : array<InspectField>) : void>

//! Таблица «имя типа -> генератор описаний полей».
var public g_decs_inspect_fields : table<string; InspectFieldsFn>

//! Отбрасывает квалификацию модуля: ``decs::EntityId`` -> ``EntityId``.
//! Тип в CTypeInfo.fullName и в typeinfo полей приходит с именем модуля,
//! а таблицы регистрации и ConfigFormat работают с короткими именами.
def public NormalizeTypeName(name : string) : string {
    let idx = last_index_of(name, "::")
    return idx >= 0 ? slice(name, idx + 2, length(name)) : name
}

//! Собирает описания полей структуры TT. Вызывается из сгенерированных функций.
//! apply разворачивает обход полей на этапе компиляции, поэтому имена, типы,
//! смещения и аннотации полей известны без runtime reflection.
def public CollectInspectFields(t : type<auto(TT)>; var out : array<InspectField>) {
    var value := default<TT>
    unsafe {
        let base = reinterpret<uint64>(addr(value))
        apply(value) $(name : string; field; annotations) {
            var field_desc : InspectField
            field_desc.name = name
            field_desc.typeName = NormalizeTypeName(typeinfo stripped_typename(type<typedecl(field)>))
            field_desc.offset = reinterpret<uint64>(addr(field)) - base
            for (ann in annotations) {
                field_desc.annotations |> push(ann)
            }
            out |> emplace(field_desc)
        }
    }
}

//! Записывает значение типа TT по указателю в ConfigFormat.
//! CF — та же apply-based точка, что использует загрузка через from_CF:
//! аннотации @rename / @optional / @embed и т.п. обрабатываются одинаково.
def public WriteValue(t : type<auto(TT)>; ptr : void?) : StructField? {
    unsafe {
        var cf = CF(*reinterpret<TT?>(ptr))
        static_if (typeinfo is_struct(type<TT>)) {
            if (cf != null) {
                cf.typeName = NormalizeTypeName(typeinfo stripped_typename(type<TT>))
            }
        }
        return <- cf
    }
}

//! Универсальная точка входа при загрузке сцены.
//! Возвращает false, если тип не зарегистрирован (не помечен DecsSerializable).
def public DecsAddComponent(var cmp : decs::ComponentMap;
                            type_name : string;
                            component_name : string;
                            cf : StructField? const) : bool {
    let key = NormalizeTypeName(type_name)
    if (!g_decs_adders.key_exists(key)){
        return false
    }
    invoke(g_decs_adders[key], cmp, component_name, cf)
    return true
}

//! Универсальная точка входа при сохранении сцены.
//! Возвращает null, если тип не зарегистрирован (не помечен DecsSerializable).
def public DecsWriteComponent(ptr : void?; type_name : string; component_name : string) : StructField? {
    let key = NormalizeTypeName(type_name)
    if (!key_exists(g_decs_writers, key)) {
        return null
    }
    var cf = invoke(g_decs_writers[key], ptr)
    if (cf != null) {
        cf.name = component_name
    }
    return <- cf
}

//! Есть ли для типа сгенерированная схема инспектора.
def public DecsHasInspectSchema(type_name : string) : bool {
    return key_exists(g_decs_inspect_fields, NormalizeTypeName(type_name))
}

//! Описание полей для инспектора. false — для типа нет сгенерированной схемы.
def public DecsInspectFields(type_name : string; var out : array<InspectField>) : bool {
    let key = NormalizeTypeName(type_name)
    if (!key_exists(g_decs_inspect_fields, key)) {
        return false
    }
    invoke(g_decs_inspect_fields[key], out)
    return true
}

//! Регистрирует записыватель для базового типа (ключами служат имена типов daslang).
def private RegisterScalarWriter(name : string; t : auto(TT)) {
    g_decs_writers[name] = @@(ptr : void?) : StructField? {
        return <- WriteValue(type<TT>, ptr)
    }
}

def private WriteEntityId(ptr : void?) : StructField? => WriteValue(type<EntityId>, ptr)

def private InspectEntityId(var out : array<InspectField>) => CollectInspectFields(type<EntityId>, out)

[init]
def RegisterBuiltinWriters() {
    RegisterScalarWriter("float", type<float>)
    RegisterScalarWriter("double", type<double>)
    RegisterScalarWriter("int", type<int>)
    RegisterScalarWriter("uint", type<uint>)
    RegisterScalarWriter("int8", type<int8>)
    RegisterScalarWriter("uint8", type<uint8>)
    RegisterScalarWriter("int16", type<int16>)
    RegisterScalarWriter("uint16", type<uint16>)
    RegisterScalarWriter("int64", type<int64>)
    RegisterScalarWriter("uint64", type<uint64>)
    RegisterScalarWriter("bool", type<bool>)
    RegisterScalarWriter("string", type<string>)
    RegisterScalarWriter("float2", type<float2>)
    RegisterScalarWriter("float3", type<float3>)
    RegisterScalarWriter("float4", type<float4>)
    RegisterScalarWriter("int2", type<int2>)
    RegisterScalarWriter("int3", type<int3>)
    RegisterScalarWriter("int4", type<int4>)
    RegisterScalarWriter("uint2", type<uint2>)
    RegisterScalarWriter("uint3", type<uint3>)
    RegisterScalarWriter("uint4", type<uint4>)
    RegisterScalarWriter("float3x3", type<float3x3>)
    RegisterScalarWriter("float3x4", type<float3x4>)
    RegisterScalarWriter("float4x4", type<float4x4>)
    g_decs_writers["EntityId"] = @@WriteEntityId
    g_decs_inspect_fields["EntityId"] = @@InspectEntityId
}

//! Помечает структуру компонента как сериализуемую через ConfigFormat.
//!
//! Генерирует три вещи, все — на этапе компиляции:
//!  * adder  — ``from_CF`` + ``decs::set`` (загрузка сцены);
//!  * writer — ``CF`` по указателю на значение (сохранение сцены);
//!  * схему инспектора (имена, типы, смещения и аннотации полей через ``apply``).
//!
//! Для ``decs_template``-структур adder раскладывает поля на префиксные компоненты
//! через ``apply_decs_template``; writer не нужен — такие поля сохраняются поштучно.
//!
//! Модуль структуры должен видеть ``ConfigFormatBoost`` (``from_CF``/``CF``) — так же,
//! как этого требует загрузка сцены.
[structure_macro(name="DecsSerializable")]
class DecsSerializableMacro : AstStructureAnnotation {
    def override apply(var st : StructurePtr; var group : ModuleGroup;
                       var args : AnnotationArgumentList; var errors : das_string) : bool {
        let type_name = st.name.string()
        let fn_init_name = "register_decs_adder_{type_name}"

        // Проверяем, помечена ли структура как decs_template
        var is_template = false
        for (ann in st.annotations) {
            if (ann.annotation.name == "decs_template") {
                is_template = true
                break
            }
        }

        if (is_template) {
            // Для шаблона: CF -> struct -> apply_decs_template (раскладывает на префиксные компоненты)
            var fnInit <- qmacro_function(fn_init_name) $() {
                g_decs_adders[$v(type_name)] = @@(var cmp : decs::ComponentMap;
                                                  component_name : string;
                                                  cf : StructField? const) : void {
                    var value <- from_CF(cf, default<$t(st)>)
                    apply_decs_template(cmp, value)
                }
            }
            fnInit.flags |= FunctionFlags.init
            if (!(compiling_module() |> add_function(fnInit))) {
                panic("decs_serializable: failed to add {fn_init_name}")
                return false
            }
        } else {
            // Обычная ветка: загрузка в компонент с тем же именем, запись — через CF.
            var fnInit <- qmacro_function(fn_init_name) $() {
                g_decs_adders[$v(type_name)] = @@(var cmp : decs::ComponentMap;
                                                  component_name : string;
                                                  cf : StructField? const) : void {
                    var value <- from_CF(cf, default<$t(st)>)
                    decs::set(cmp, component_name, value)
                }
                g_decs_writers[$v(type_name)] = @@(ptr : void?) : StructField? {
                    return <- WriteValue(type<$t(st)>, ptr)
                }
            }
            fnInit.flags |= FunctionFlags.init
            if (!(compiling_module() |> add_function(fnInit))) {
                panic("decs_serializable: failed to add {fn_init_name}")
                return false
            }
        }

        let fn_inspect_name = "register_decs_inspect_{type_name}"
        var fnInspect <- qmacro_function(fn_inspect_name) $() {
            g_decs_inspect_fields[$v(type_name)] = @@(var out : array<InspectField>) : void {
                CollectInspectFields(type<$t(st)>, out)
            }
        }
        fnInspect.flags |= FunctionFlags.init
        if (!(compiling_module() |> add_function(fnInspect))) {
            errors := "decs_serializable: failed to add {fn_inspect_name}"
            return false
        }
        return true
    }
}

def RegisterEntityIdImpl(var cmp : decs::ComponentMap; component_name : string; cf : StructField? const) {
    var value <- from_CF(cf, default<EntityId>)
    decs::set(cmp, component_name, value)
}

[init]
def RegisterEntityId() {
    g_decs_adders["EntityId"] = @@RegisterEntityIdImpl
}
