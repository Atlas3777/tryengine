options gen2
options relaxed_assign = false

module Serializable shared

require daslib/ast_boost
require daslib/decs
require daslib/decs_boost
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

//! True when a writer is registered for this type. This answers "how to write", not
//! "should it be written": scenes and prefabs ask `EcsShouldSave(component_name)`.
//! The writers are registered by `[DecsSerializable]` and by the builtin table below; a
//! struct without the annotation has none.
def public HasComponentWriter(type_name : string) : bool {
    return key_exists(g_decs_writers, NormalizeTypeName(type_name))
}

//! Собирает описания полей структуры TT. Вызывается из схемы, которую регистрирует
//! `%ecs` (tryengine/EcsMacro): apply разворачивает обход полей на этапе компиляции,
//! поэтому имена, типы, смещения и аннотации полей известны без runtime reflection.
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
//! Возвращает false, если для типа не зарегистрирован adder.
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
//! Возвращает null, если для типа не зарегистрирован writer.
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

//! Значение компонента архетипа -> ConfigFormat. `index` — строка в архетипе.
//! Общая точка для сохранения сцены и префаба: смещение и тип берутся из Component.
def public DecsComponentToCF(comp : decs::Component&; index : int64) : StructField? {
    let offset = index * int64(comp.stride)
    unsafe {
        let ptr = addr<void?>(comp.data[offset])
        return DecsWriteComponent(ptr, comp.info.fullName, comp.name)
    }
}

//! Применяет одно поле ConfigFormat к ComponentMap по его имени в DECS.
//! Общая точка для загрузки сцены и префаба: тип берётся из уже известных
//! компонентов архетипа, а если его там ещё нет — из варианта значения.
//! Возвращает false для null/неподдерживаемого значения; вызывающий решает,
//! считать это ошибкой.
def public ApplyDecsConfigField(var cmp : decs::ComponentMap;
                                component_name : string;
                                cf : StructField? const;
                                type_name : string) : bool {
    if (cf == null || cf.value is _null) {
        return false
    }

    if (decsState.componentTypeCheck |> key_exists(component_name)) {
        let info = decsState.componentTypeCheck |> get_value(component_name)
        if (info.basicType == Type.tFloat) {
            cmp |> set(component_name, from_CF(cf, type<float>))
            return true
        } elif (info.basicType == Type.tFloat2) {
            cmp |> set(component_name, from_CF(cf, type<float2>))
            return true
        } elif (info.basicType == Type.tFloat3) {
            cmp |> set(component_name, from_CF(cf, type<float3>))
            return true
        } elif (info.basicType == Type.tFloat4) {
            cmp |> set(component_name, from_CF(cf, type<float4>))
            return true
        } elif (info.basicType == Type.tInt) {
            cmp |> set(component_name, from_CF(cf, type<int>))
            return true
        } elif (info.basicType == Type.tInt2) {
            cmp |> set(component_name, from_CF(cf, type<int2>))
            return true
        } elif (info.basicType == Type.tInt3) {
            cmp |> set(component_name, from_CF(cf, type<int3>))
            return true
        } elif (info.basicType == Type.tInt4) {
            cmp |> set(component_name, from_CF(cf, type<int4>))
            return true
        } elif (info.basicType == Type.tUInt) {
            cmp |> set(component_name, from_CF(cf, type<uint>))
            return true
        } elif (info.basicType == Type.tUInt64) {
            cmp |> set(component_name, from_CF(cf, type<uint64>))
            return true
        } elif (info.basicType == Type.tInt64) {
            cmp |> set(component_name, from_CF(cf, type<int64>))
            return true
        } elif (info.basicType == Type.tBool) {
            cmp |> set(component_name, from_CF(cf, type<bool>))
            return true
        } elif (info.basicType == Type.tString) {
            cmp |> set(component_name, from_CF(cf, type<string>))
            return true
        } elif (info.basicType == Type.tStructure) {
            return DecsAddComponent(cmp, type_name, component_name, cf)
        }
    }

    assume val = cf.value
    if (val is _float) {
        cmp |> set(component_name, val as _float)
        return true
    } elif (val is _float2) {
        cmp |> set(component_name, val as _float2)
        return true
    } elif (val is _float3) {
        cmp |> set(component_name, val as _float3)
        return true
    } elif (val is _float4) {
        cmp |> set(component_name, val as _float4)
        return true
    } elif (val is _int) {
        cmp |> set(component_name, val as _int)
        return true
    } elif (val is _int2) {
        cmp |> set(component_name, val as _int2)
        return true
    } elif (val is _int3) {
        cmp |> set(component_name, val as _int3)
        return true
    } elif (val is _int4) {
        cmp |> set(component_name, val as _int4)
        return true
    } elif (val is _uint) {
        cmp |> set(component_name, val as _uint)
        return true
    } elif (val is _uint64) {
        cmp |> set(component_name, val as _uint64)
        return true
    } elif (val is _int64) {
        cmp |> set(component_name, val as _int64)
        return true
    } elif (val is _bool) {
        cmp |> set(component_name, val as _bool)
        return true
    } elif (val is _string) {
        cmp |> set(component_name, val as _string)
        return true
    } elif (val is _object) {
        return DecsAddComponent(cmp, type_name, component_name, cf)
    }

    return false
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

//! Помечает структуру как значение, которое можно писать и читать ConfigFormat.
//!
//! Это физическая возможность типа, а не компонент: генерируются adder
//! (`from_CF` + `decs::set`, загрузка сцены/префаба) и writer (`CF` по значению,
//! сохранение). Компонентом тип делает `%ecs` (tryengine/EcsMacro), и он же решает,
//! сохраняется компонент (`transient`) и показывается (`hidden`).
//!
//! `[decs_template]`-структуру помечать не нужно и нельзя: она хранится не одним
//! значением, а префиксными полями, и каждое поле пишется по типу своего поля.
[structure_macro(name="DecsSerializable")]
class DecsSerializableMacro : AstStructureAnnotation {
    def override apply(var st : StructurePtr; var group : ModuleGroup;
                       var args : AnnotationArgumentList; var errors : das_string) : bool {
        let type_name = st.name.string()
        var fn_init <- qmacro_function("register_decs_value_{type_name}") $() {
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
        fn_init.flags.init = true
        if (!(compiling_module() |> add_function(fn_init))) {
            errors := "DecsSerializable: can't add registration for {type_name}"
            return false
        }
        return true
    }
}

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

def RegisterEntityIdImpl(var cmp : decs::ComponentMap; component_name : string; cf : StructField? const) {
    var value <- from_CF(cf, default<EntityId>)
    decs::set(cmp, component_name, value)
}

[init]
def RegisterEntityId() {
    g_decs_adders["EntityId"] = @@RegisterEntityIdImpl
}
