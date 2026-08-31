options gen2
options indenting = 4
options strict_smart_pointers = true

module ConfigFormatBoost shared public

//! ConfigFormat ↔ struct automatic serialization and boost utilities.
//!
//! Generates ``from_CF`` and ``CF`` conversions for user structs using
//! compile-time reflection. Also provides the ``%config~`` reader macro
//! for embedding config literals in source code.
//! Requires the ``ConfigFormat`` module.

require daslib/rtti public
require daslib/ast
require math
require strings
require ConfigFormat public
require daslib/ast_boost
require daslib/contracts
require daslib/templates
require daslib/templates_boost public
require daslib/strings_boost
require daslib/apply
require daslib/macro_boost
require daslib/static_let
require daslib/enum_trait
require daslib/constant_expression

def operator ?[] (a : ConfigFormat::StructField const? ==const; key : string) : StructField? const {
    //! Returns the field of the key in the Config object, if it exists.
    return a != null && a.value is _object ? (a.value as _object)?[key] ?? null : null
}

def operator ?[] (var a : ConfigFormat::StructField? ==const; key : string) : StructField? {
    //! Returns the field of the key in the Config object, if it exists.
    return a != null && a.value is _object ? (a.value as _object)?[key] ?? null : null
}

def operator ?.(a : ConfigFormat::StructField const? ==const; key : string) : StructField? const {
    //! Returns the field of the key in the Config object, if it exists.
    return a != null && a.value is _object ? (a.value as _object)?[key] ?? null : null
}

def operator ?.(var a : ConfigFormat::StructField? ==const; key : string) : StructField? {
    //! Returns the field of the key in the Config object, if it exists.
    return a != null && a.value is _object ? (a.value as _object)?[key] ?? null : null
}

def operator ?[] (a : ConfigFormat::StructField const? ==const; idx : int) : StructField? const {
    //! Returns the field of the index in the Config array, if it exists.
    return a != null && a.value is _array ? (a.value as _array)?[idx] ?? null : null
}

def operator ?[] (var a : ConfigFormat::StructField? ==const; idx : int) : StructField? {
    //! Returns the field of the index in the Config array, if it exists.
    return a != null && a.value is _array ? (a.value as _array)?[idx] ?? null : null
}

def private null_coalescing(a : ConfigFormat::StructField const?; val : auto(TT)) : TT {
    //! Returns the scalar value of the Config field, if it exists, otherwise returns default.
    if (a != null) {
        if (a.value is _double) return TT(a.value as _double)
        elif (a.value is _float) return TT(a.value as _float)
        elif (a.value is _int64) return TT(a.value as _int64)
        elif (a.value is _int) return TT(a.value as _int)
        elif (a.value is _int16) return TT(a.value as _int16)
        elif (a.value is _int8) return TT(a.value as _int8)
        elif (a.value is _uint64) return TT(a.value as _uint64)
        elif (a.value is _uint) return TT(a.value as _uint)
        elif (a.value is _uint16) return TT(a.value as _uint16)
        elif (a.value is _uint8) return TT(a.value as _uint8)
    }
    return val
}

def private null_coalescing_with_string(a : ConfigFormat::StructField const?; val : auto(TT)) : TT {
    //! Returns the scalar or string-converted value of the Config field.
    if (a != null) {
        if (a.value is _double) return TT(a.value as _double)
        elif (a.value is _float) return TT(a.value as _float)
        elif (a.value is _int64) return TT(a.value as _int64)
        elif (a.value is _int) return TT(a.value as _int)
        elif (a.value is _int16) return TT(a.value as _int16)
        elif (a.value is _int8) return TT(a.value as _int8)
        elif (a.value is _uint64) return TT(a.value as _uint64)
        elif (a.value is _uint) return TT(a.value as _uint)
        elif (a.value is _uint16) return TT(a.value as _uint16)
        elif (a.value is _uint8) return TT(a.value as _uint8)
        elif (a.value is _string) return TT(a.value as _string)
    }
    return val
}

def operator ??(a : StructField const?; val : double) : double => null_coalescing(a, val)
def operator ??(a : StructField const?; val : float) : float => null_coalescing(a, val)
def operator ??(a : StructField const?; val : int8) : int8 => null_coalescing(a, val)
def operator ??(a : StructField const?; val : int16) : int16 => null_coalescing(a, val)
def operator ??(a : StructField const?; val : int) : int => null_coalescing(a, val)
def operator ??(a : StructField const?; val : int64) : int64 => null_coalescing_with_string(a, val)
def operator ??(a : StructField const?; val : uint8) : uint8 => null_coalescing(a, val)
def operator ??(a : StructField const?; val : uint16) : uint16 => null_coalescing(a, val)
def operator ??(a : StructField const?; val : uint) : uint => null_coalescing(a, val)
def operator ??(a : StructField const?; val : uint64) : uint64 => null_coalescing_with_string(a, val)

def operator ??(a : StructField const?; val : bool) : bool {
    return a != null && a.value is _bool ? a.value as _bool : val
}

def operator ??(a : StructField const?; val : string) : string {
    return a != null && a.value is _string ? a.value as _string : val
}

def operator ?. value(a : StructField const? ==const) : ConfigValue const? {
    return a . ?. value
}

def operator ?. value(var a : StructField? ==const) : ConfigValue? {
    return a . ?. value
}

[macro_function]
def private is_config_ptr_value(td : TypeDeclPtr) {
    if (td.baseType != Type.tPointer || td.firstType == null || td.firstType.baseType != Type.tStructure) return false
    let st = td.firstType.structType
    return st.name == "StructField" && st._module.name == "ConfigFormat"
}

[variant_macro(name="better_config")]
class private BetterConfigMacro : AstVariantMacro {
    def override visitExprIsVariant(prog : ProgramPtr; mod : Module?; expr : ExprIsVariant?) : ExpressionPtr {
        if (is_config_ptr_value(expr.value._type)) {
            var vdr = new ExprField(at = expr.at, name := "value", value <- clone_expression(expr.value))
            var isv = new ExprIsVariant(at = expr.at, name := expr.name, value <- vdr)
            isv.genFlags = expr.genFlags
            return <- isv
        }
        return null
    }
    def override visitExprAsVariant(prog : ProgramPtr; mod : Module?; expr : ExprAsVariant?) : ExpressionPtr {
        if (is_config_ptr_value(expr.value._type)) {
            var vdr = new ExprField(at = expr.at, name := "value", value <- clone_expression(expr.value))
            var isv = new ExprAsVariant(at = expr.at, name := expr.name, value <- vdr)
            isv.genFlags = expr.genFlags
            return <- isv
        }
        return null
    }
    def override visitExprSafeAsVariant(prog : ProgramPtr; mod : Module?; expr : ExprSafeAsVariant?) : ExpressionPtr {
        if (is_config_ptr_value(expr.value._type)) {
            var vdr = new ExprField(at = expr.at, name := "value", value <- clone_expression(expr.value))
            var isv = new ExprSafeAsVariant(at = expr.at, name := expr.name, value <- vdr)
            isv.genFlags = expr.genFlags
            return <- isv
        }
        return null
    }
}

[reader_macro(name="config")]
class private ConfigReader : AstReaderMacro {
    def override accept(prog : ProgramPtr; mod : Module?; var expr : ExprReader?; ch : int; info : LineInfo) : bool {
        append(expr.sequence, ch)
        if (ends_with(expr.sequence, "%%")) {
            let len = length(expr.sequence)
            resize(expr.sequence, len - 2)
            return false
        } else {
            return true
        }
    }
    def override visit(prog : ProgramPtr; mod : Module?; expr : ExprReader?) : ExpressionPtr {
        let seqStr = string(expr.sequence)
        var error = ""
        var cfg = read_config(seqStr, error)
        macro_verify(cfg != null, prog, expr.at, "config did not parse ({error})")
        return <- convert_to_expression(cfg, expr.at)
    }
}

[expect_any_enum(ent), template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : auto(EnumT); defV : EnumT = default<EnumT>) : EnumT {
    if (v == null) return defV
    let val & = v.value
    if (val is _string) {
        let name = val as _string
        let ti = typeinfo rtti_typeinfo(type<EnumT>)
        for (ef in *ti.enumType) {
            return unsafe(reinterpret<EnumT>(ef.value)) if (name == ef.name)
        }
        panic("not a valid enumeration {name} in {typeinfo typename(type<EnumT>)}")
    } else {
        var intValue = 0
        if (val is _int) {
            intValue = val as _int
        } elif (val is _int64) {
            intValue = int(val as _int64)
        } elif (val is _int16) {
            intValue = int(val as _int16)
        } elif (val is _int8) {
            intValue = int(val as _int8)
        } elif (val is _uint) {
            intValue = int(val as _uint)
        } elif (val is _uint64) {
            intValue = int(val as _uint64)
        } elif (val is _uint16) {
            intValue = int(val as _uint16)
        } elif (val is _uint8) {
            intValue = int(val as _uint8)
        } elif (val is _double) {
            intValue = int(val as _double)
        } elif (val is _float) {
            intValue = int(val as _float)
        } else return defV
        for (it in defV) {
            if (intValue == int(it)) return it
        }
        panic("not a valid enumeration {intValue} in {typeinfo typename(type<EnumT>)}")
    }
    return defV
}

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : string; defV : string = "") {
    if (v == null || !(v.value is _string)) return defV
    return v.value as _string
}

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : bool; defV : bool = false) {
    if (v == null || !(v.value is _bool)) return defV
    return v.value as _bool
}

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : float; defV : float = 0.) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : double; defV : double = 0.lf) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : int; defV : int = 0) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : uint; defV : uint = 0u) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : int64; defV : int64 = 0l) => null_coalescing_with_string(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : uint64; defV : uint64 = 0ul) => null_coalescing_with_string(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : int8; defV : int8 = int8(0)) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : uint8; defV : uint8 = uint8(0)) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : int16; defV : int16 = int16(0)) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : uint16; defV : uint16 = uint16(0)) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : bitfield; defV : bitfield = bitfield()) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : bitfield : uint8 < >; defV : bitfield8 = bitfield8()) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : bitfield : uint16 < >; defV : bitfield16 = bitfield16()) => null_coalescing(v, defV)

[generic, template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : bitfield : uint64 < >; defV : bitfield64 = bitfield64()) => null_coalescing(v, defV)

[expect_any_vector_type(v)]
def CF(v : auto(VecT)) : StructField? {
    static_if (typeinfo stripped_typename(type<VecT>) == "float2") {
        return new StructField(typeName = "f32x2", value <- ConfigValue(_float2 = v))
    } static_elif (typeinfo stripped_typename(type<VecT>) == "float3") {
        return new StructField(typeName = "f32x3", value <- ConfigValue(_float3 = v))
    } static_elif (typeinfo stripped_typename(type<VecT>) == "float4") {
        return new StructField(typeName = "f32x4", value <- ConfigValue(_float4 = v))
    } static_elif (typeinfo stripped_typename(type<VecT>) == "int2") {
        return new StructField(typeName = "i32x2", value <- ConfigValue(_int2 = v))
    } static_elif (typeinfo stripped_typename(type<VecT>) == "int3") {
        return new StructField(typeName = "i32x3", value <- ConfigValue(_int3 = v))
    } static_elif (typeinfo stripped_typename(type<VecT>) == "int4") {
        return new StructField(typeName = "i32x4", value <- ConfigValue(_int4 = v))
    } static_elif (typeinfo stripped_typename(type<VecT>) == "uint2") {
        return new StructField(typeName = "u32x2", value <- ConfigValue(_uint2 = v))
    } static_elif (typeinfo stripped_typename(type<VecT>) == "uint3") {
        return new StructField(typeName = "u32x3", value <- ConfigValue(_uint3 = v))
    } static_elif (typeinfo stripped_typename(type<VecT>) == "uint4") {
        return new StructField(typeName = "u32x4", value <- ConfigValue(_uint4 = v))
    } else {
        static_if (typeinfo vector_dim(type<VecT>) == 2) {
            return CF({"x" => CF(v.x), "y" => CF(v.y)})
        } static_elif (typeinfo vector_dim(type<VecT>) == 3) {
            return CF({"x" => CF(v.x), "y" => CF(v.y), "z" => CF(v.z)})
        } static_elif (typeinfo vector_dim(type<VecT>) == 4) {
            return CF({"x" => CF(v.x), "y" => CF(v.y), "z" => CF(v.z), "w" => CF(v.w)})
        }
    }
}

[expect_any_vector_type(ent), template(ent), unused_argument(ent)]
def from_CF(v : StructField const explicit?; ent : auto(VecT); defV : VecT = VecT()) {
    if (v == null){
        return defV
    }
    unsafe {
        let val & = v.value
        static_if (typeinfo stripped_typename(type<VecT>) == "float2") {
            if (val is _float2) return val as _float2
        } static_elif (typeinfo stripped_typename(type<VecT>) == "float3") {
            if (val is _float3) return val as _float3
        } static_elif (typeinfo stripped_typename(type<VecT>) == "float4") {
            if (val is _float4) return val as _float4
        } static_elif (typeinfo stripped_typename(type<VecT>) == "int2") {
            if (val is _int2) return val as _int2
        } static_elif (typeinfo stripped_typename(type<VecT>) == "int3") {
            if (val is _int3) return val as _int3
        } static_elif (typeinfo stripped_typename(type<VecT>) == "int4") {
            if (val is _int4) return val as _int4
        } static_elif (typeinfo stripped_typename(type<VecT>) == "uint2") {
            if (val is _uint2) return val as _uint2
        } static_elif (typeinfo stripped_typename(type<VecT>) == "uint3") {
            if (val is _uint3) return val as _uint3
        } static_elif (typeinfo stripped_typename(type<VecT>) == "uint4") {
            if (val is _uint4) return val as _uint4
        }

        if (val is _array) {
            unsafe {
                let arr & = val as _array
                let n = length(arr)
                static_if (typeinfo vector_dim(defV) == 2) {
                    return VecT(
                        n > 0 ? (arr[0] ?? defV.x) : defV.x,
                        n > 1 ? (arr[1] ?? defV.y) : defV.y)
                } static_elif (typeinfo vector_dim(defV) == 3) {
                    return VecT(
                        n > 0 ? (arr[0] ?? defV.x) : defV.x,
                        n > 1 ? (arr[1] ?? defV.y) : defV.y,
                        n > 2 ? (arr[2] ?? defV.z) : defV.z)
                } static_elif (typeinfo vector_dim(defV) == 4) {
                    return VecT(
                        n > 0 ? (arr[0] ?? defV.x) : defV.x,
                        n > 1 ? (arr[1] ?? defV.y) : defV.y,
                        n > 2 ? (arr[2] ?? defV.z) : defV.z,
                        n > 3 ? (arr[3] ?? defV.w) : defV.w)
                }
            }
        }
        assume vo = v
        static_if (typeinfo vector_dim(defV) == 2) {
            return VecT(vo?["x"] ?? defV.x, vo?["y"] ?? defV.y)
        } static_elif (typeinfo vector_dim(defV) == 3) {
            return VecT(vo?["x"] ?? defV.x, vo?["y"] ?? defV.y, vo?["z"] ?? defV.z)
        } static_elif (typeinfo vector_dim(defV) == 4) {
            return VecT(vo?["x"] ?? defV.x, vo?["y"] ?? defV.y, vo?["z"] ?? defV.z, vo?["w"] ?? defV.w)
        }
    }
}

[template(anything), unused_argument(anything)]
def from_CF(v : StructField const explicit?; anything : table< auto(KT); auto(VT) >) {
    var ret : table<KT; VT>
    if (v == null || !(v.value is _object)) return <- ret
    unsafe {
        let tab & = v.value as _object
        for (k, vv in keys(tab), values(tab)) {
            move_to_ref(ret[k], _::from_CF(vv, type<VT -&>))
        }
    }
    return <- ret
}

struct public ConfigFieldState {
    argName : string        //! name of the field in Config
    enumAsInt : bool        //! whether to parse enum as integer
    unescape : bool         //! whether to unescape strings
    embed : bool            //! whether to embed the field
    optional : bool         //! whether the field is optional
}

def public parse_config_annotation(name : string; annotation : array<tuple<name : string; data : RttiValue>>) : ConfigFieldState {
    var fieldState : ConfigFieldState
    fieldState.argName = name
    for (ann in annotation) {
        if (ann.name == "rename") {
            if (ann.data is tString) {
                fieldState.argName = ann.data as tString
            } elif (ann.data is tBool && !empty(name) && first_character(name) == '_') {
                fieldState.argName = slice(name, 1)
            }
        } elif (ann.name == "enum_as_int" && ann.data is tBool) {
            fieldState.enumAsInt = ann.data as tBool
        } elif (ann.name == "unescape" && ann.data is tBool) {
            fieldState.unescape = ann.data as tBool
        } elif (ann.name == "embed" && ann.data is tBool) {
            fieldState.embed = ann.data as tBool
        } elif (ann.name == "optional" && ann.data is tBool) {
            fieldState.optional = ann.data as tBool
        }
    }
    return <- fieldState
}

[unused_argument(anything)]
def from_CF(v : StructField const explicit?; anything : auto(TT)) {
    static_if (typeinfo is_dim(anything)) {
        var ret : TT -const -&
        if (v == null || !(v.value is _array)) return <- ret
        unsafe {
            let arr & = v.value as _array
            if (arr |> length != typeinfo dim(anything)) return <- ret
            for (i in 0..typeinfo dim(anything)) {
                move_to_ref(ret[i], _::from_CF(arr[i], type<typedecl(anything[0])>))
            }
            return <- ret
        }
    } static_elif (typeinfo is_struct(anything)) {
        var ret <- default<TT -const>
        if (v == null || !(v.value is _object)) return <- ret
        apply(ret) $(name : string; var field; annotations) {
            static_let("{typeinfo typename(type<TT-const-&>)}`{name}") {
                var fieldState : ConfigFieldState
                var fieldStateParsed : bool = false
            }
            if (!fieldStateParsed) {
                fieldState <- parse_config_annotation(name, annotations)
                fieldStateParsed = true
            }
            (v.value as _object) |> get(fieldState.argName) $(val) {
                move_to_ref(field, _::from_CF(val, type<typedecl(field)>))
            }
        }
        return <- ret
    } static_elif (typeinfo is_tuple(anything)) {
        var ret <- default<TT -const>
        if (v == null || !(v.value is _object)) return <- ret
        apply(ret) $(name : string; var field) {
            (v.value as _object) |> get(name) $(val) {
                move_to_ref(field, _::from_CF(val, type<typedecl(field)>))
            }
        }
        return <- ret
    } static_elif (typeinfo is_array(anything)) {
        var ret : TT
        if (v == null || !(v.value is _array)) return <- ret
        unsafe {
            let arr & = v.value as _array
            ret |> reserve(arr |> long_length)
            for (a in arr) {
                static_if (typeinfo can_copy(anything[0])) {
                    ret |> push_clone <| _::from_CF(a, type<typedecl(anything[0])>)
                } else {
                    ret |> emplace <| _::from_CF(a, type<typedecl(anything[0])>)
                }
            }
            return <- ret
        }
    } static_elif (typeinfo is_variant(anything)) {
        var ret : TT
        if (v == null || !(v.value is _object)) return <- ret
        let vv : StructField? = (v.value as _object)?["$variant"] ?? default<StructField?>
        if (vv == null) return <- ret
        var index = -1
        if (vv.value is _int) {
            index = vv.value as _int
        } elif (vv.value is _int64) {
            index = int(vv.value as _int64)
        } elif (vv.value is _int16) {
            index = int(vv.value as _int16)
        } elif (vv.value is _int8) {
            index = int(vv.value as _int8)
        } elif (vv.value is _uint) {
            index = int(vv.value as _uint)
        } elif (vv.value is _uint64) {
            index = int(vv.value as _uint64)
        } elif (vv.value is _uint16) {
            index = int(vv.value as _uint16)
        } elif (vv.value is _uint8) {
            index = int(vv.value as _uint8)
        } elif (vv.value is _double) {
            index = int(vv.value as _double)
        }
        if (index < 0) return <- ret
        unsafe(set_variant_index(ret, index))
        apply(ret) $(name : string; var field) {
            (v.value as _object) |> get(name) $(val) {
                move_to_ref(field, _::from_CF(val, type<typedecl(field)>))
            }
        }
        return <- ret
    } else {
        concept_assert(false, "unsupported config serialization type. missing 'from_CF")
    }
}

def CF(v : string) : StructField? => new StructField(typeName = "string", value <- ConfigValue(_string = v))
def CF(v : double) : StructField? => new StructField(typeName = "f64", value <- ConfigValue(_double = v))
def CF(v : float) : StructField? => new StructField(typeName = "f32", value <- ConfigValue(_float = v))
def CF(v : int64) : StructField? => new StructField(typeName = "i64", value <- ConfigValue(_int64 = v))
def CF(v : int) : StructField? => new StructField(typeName = "i32", value <- ConfigValue(_int = v))
def CF(v : int16) : StructField? => new StructField(typeName = "i16", value <- ConfigValue(_int16 = v))
def CF(v : int8) : StructField? => new StructField(typeName = "i8", value <- ConfigValue(_int8 = v))
def CF(v : uint64) : StructField? => new StructField(typeName = "u64", value <- ConfigValue(_uint64 = v))
def CF(v : uint) : StructField? => new StructField(typeName = "u32", value <- ConfigValue(_uint = v))
def CF(v : uint16) : StructField? => new StructField(typeName = "u16", value <- ConfigValue(_uint16 = v))
def CF(v : uint8) : StructField? => new StructField(typeName = "u8", value <- ConfigValue(_uint8 = v))
def CF(v : bool) : StructField? => new StructField(typeName = "bool", value <- ConfigValue(_bool = v))
def CF(v : float3x3) : StructField? => new StructField(typeName = "f32m3x3", value <- ConfigValue(_float3x3 = v))
def CF(v : float3x4) : StructField? => new StructField(typeName = "f32m3x4", value <- ConfigValue(_float3x4 = v))
def CF(v : float4x4) : StructField? => new StructField(typeName = "f32m4x4", value <- ConfigValue(_float4x4 = v))
def CF(var v : table<string; StructField?>) : StructField? => new StructField(typeName = "Object", value <- ConfigValue(_object <- v))

def CF(var v : array<StructField?>) : StructField? {
    var tName = "void[]"
    if (length(v) > 0 && v[0] != null && !empty(v[0].typeName)) {
        tName = "{v[0].typeName}[]"
    }
    return new StructField(typeName = tName, value <- ConfigValue(_array <- v))
}

def CF(value : auto(TT)) : StructField? {
    static_if (typeinfo is_dim(value)) {
        var arr : array<StructField?>
        arr |> reserve(length(value))
        for (t in value) {
            arr |> push(CF(t))
        }
        var res = _::CF(arr)
        var dummy = _::CF(default<typedecl(value[0])>)
        if (res != null && dummy != null && !empty(dummy.typeName)) {
            res.typeName = "{dummy.typeName}[]"
        }
        return res
    }
    static_if (typeinfo typename(value) == "ConfigFormat::StructField?" || typeinfo typename(value) == "ConfigFormat::StructField? const") {
        unsafe {
            return reinterpret<StructField?>(value)
        }
    } static_elif (typeinfo is_pointer(value)) {
        return value == null ? default<StructField?> : _::CF(*value)
    } static_elif (typeinfo is_enum(value)) {
        return CF("{value}")
    } static_elif (typeinfo is_string(value)) {
        return _::CF(value)
    } static_elif (typeinfo is_struct(value)) {
        var map : table<string; StructField?>
        apply(value) $(name : string; field; annotations) {
            static_if (typeinfo is_function(field)) {
                pass
            } static_elif (typeinfo is_void_pointer(field)) {
                pass
            } static_elif (typeinfo is_pointer(field)) {
                if (field != null) {
                    static_let("{typeinfo typename(type<TT-const-&>)}`{name}") {
                        var fieldState : ConfigFieldState
                        var fieldStateParsed : bool = false
                    }
                    if (!fieldStateParsed) {
                        fieldState <- parse_config_annotation(name, annotations)
                        fieldStateParsed = true
                    }
                    var sub = _::CF(field)
                    if (sub != null) { sub.name = fieldState.argName; }
                    map |> insert(fieldState.argName, sub)
                }
            } else {
                static_let("{typeinfo typename(type<TT-const-&>)}`{name}") {
                    var fieldState : ConfigFieldState
                    var fieldStateParsed : bool = false
                }
                if (!fieldStateParsed) {
                    fieldState <- parse_config_annotation(name, annotations)
                    fieldStateParsed = true
                }
                var skip = false
                if (fieldState.optional) {
                    static_if (typeinfo is_string(field)) {
                        skip = empty(field)
                    } static_elif (typeinfo is_array(field)) {
                        skip = empty(field)
                    } static_elif (typeinfo is_table(field)) {
                        skip = empty(field)
                    } static_elif (typeinfo is_pointer(field)) {
                        skip = field == null
                    } else {
                        static_if (typeinfo is_workhorse(field)) {
                            static_if (typeinfo stripped_typename(field) == "bool") {
                                skip = !field
                            } else {
                                skip = field == default<typedecl(field)>
                            }
                        }
                    }
                }
                if (!skip) {
                    if (fieldState.enumAsInt) {
                        static_if (typeinfo is_enum(field)) {
                            let fieldStateEnumValue : int = int(field)
                            var sub = _::CF(fieldStateEnumValue)
                            if (sub != null) { sub.name = fieldState.argName; }
                            map |> insert(fieldState.argName, sub)
                        } else {
                            var sub = _::CF(field)
                            if (sub != null) { sub.name = fieldState.argName; }
                            map |> insert(fieldState.argName, sub)
                        }
                    } elif (fieldState.unescape) {
                        static_if (typeinfo is_string(field)) {
                            var sub = _::CF(unescape(field))
                            if (sub != null) { sub.name = fieldState.argName; }
                            map |> insert(fieldState.argName, sub)
                        } else {
                            var sub = _::CF(field)
                            if (sub != null) { sub.name = fieldState.argName; }
                            map |> insert(fieldState.argName, sub)
                        }
                    } elif (fieldState.embed) {
                        static_if (typeinfo is_string(field)) {
                            var errors : string
                            var subexpr = read_config(field, errors)
                            if (subexpr == null) {
                                to_log(LOG_ERROR, "CONFIG: failed to parse embedded config field '{fieldState.argName}': {errors}\n")
                                panic("failed to parse embedded config field '{fieldState.argName}': {errors}")
                            }
                            subexpr.name = fieldState.argName
                            map |> insert(fieldState.argName, subexpr)
                        } else {
                            var sub = _::CF(field)
                            if (sub != null) { sub.name = fieldState.argName; }
                            map |> insert(fieldState.argName, sub)
                        }
                    } else {
                        var sub = _::CF(field)
                        if (sub != null) { sub.name = fieldState.argName; }
                        map |> insert(fieldState.argName, sub)
                    }
                }
            }
        }
        return _::CF(map)
    } static_elif (typeinfo is_tuple(value)) {
        var map : table<string; StructField?>
        apply(value) $(name : string; field) {
            static_if (typeinfo is_function(field)) {
                pass
            } static_elif (typeinfo is_void_pointer(field)) {
                pass
            } static_elif (typeinfo is_pointer(field)) {
                if (field != null) {
                    var sub = _::CF(field)
                    if (sub != null) { sub.name = name; }
                    map |> insert(name, sub)
                }
            } else {
                var sub = _::CF(field)
                if (sub != null) { sub.name = name; }
                map |> insert(name, sub)
            }
        }
        return _::CF(map)
    } static_elif (typeinfo is_table(value)) {
        var map : table<string; StructField?>
        for (k, v in keys(value), values(value)) {
            var sub = _::CF(v)
            if (sub != null) { sub.name = k; }
            map |> insert(k, sub)
        }
        return _::CF(map)
    } static_elif (typeinfo is_iterable(value)) {
        var arr : array<StructField?>
        static_if (typeinfo is_array(value)) {
            arr |> reserve(long_length(value))
        } static_elif (typeinfo is_dim(value)) {
            arr |> reserve(typeinfo dim(value))
        }
        for (x in value) {
            arr |> push <| _::CF(x)
        }
        var res = _::CF(arr)
        static_if (typeinfo is_array(value)) {
            var dummy = _::CF(default<typedecl(value[0])>)
            if (res != null && dummy != null && !empty(dummy.typeName)) {
                res.typeName = "{dummy.typeName}[]"
            }
        }
        return res
    } static_elif (typeinfo is_variant(value)) {
        var map : table<string; StructField?>
        map |> insert("$variant", _::CF(variant_index(value)))
        apply(value) $(name : string; field) {
            var sub = _::CF(field)
            if (sub != null) { sub.name = name; }
            map |> insert(name, sub)
        }
        return _::CF(map)
    } else {
        to_log(LOG_ERROR, "CONFIG: unsupported type {typeinfo typename(value)}\n")
        concept_assert(false, "unknown CF()")
        return null
    }
}

def CF(val1; val2) : StructField? => _::CF([ _::CF(val1), _::CF(val2)])
def CF(val1; val2; val3) : StructField? => _::CF([ _::CF(val1), _::CF(val2), _::CF(val3)])
def CF(val1; val2; val3; val4) : StructField? => _::CF([ _::CF(val1), _::CF(val2), _::CF(val3), _::CF(val4)])
def CF(val1; val2; val3; val4; val5) : StructField? => _::CF([ _::CF(val1), _::CF(val2), _::CF(val3), _::CF(val4), _::CF(val5)])
def CF(val1; val2; val3; val4; val5; val6) : StructField? => _::CF([ _::CF(val1), _::CF(val2), _::CF(val3), _::CF(val4), _::CF(val5), _::CF(val6)])
def CF(val1; val2; val3; val4; val5; val6; val7) : StructField? => _::CF([ _::CF(val1), _::CF(val2), _::CF(val3), _::CF(val4), _::CF(val5), _::CF(val6), _::CF(val7)])
def CF(val1; val2; val3; val4; val5; val6; val7; val8) : StructField? => _::CF([ _::CF(val1), _::CF(val2), _::CF(val3), _::CF(val4), _::CF(val5), _::CF(val6), _::CF(val7), _::CF(val8)])
def CF(val1; val2; val3; val4; val5; val6; val7; val8; val9) : StructField? => _::CF([ _::CF(val1), _::CF(val2), _::CF(val3), _::CF(val4), _::CF(val5), _::CF(val6), _::CF(val7), _::CF(val8), _::CF(val9)])
def CF(val1; val2; val3; val4; val5; val6; val7; val8; val9; val10) : StructField? => _::CF([ _::CF(val1), _::CF(val2), _::CF(val3), _::CF(val4), _::CF(val5), _::CF(val6), _::CF(val7), _::CF(val8), _::CF(val9), _::CF(val10)])

[unused_argument(t)]
def from_config_row(cf : StructField const explicit?; t : auto(TT)) : TT {
    static_if (typeinfo is_struct(type<TT>)) {
        var ret <- default<TT -const>
        if (cf.value is _object) {
            apply_imm(ret) $(name : string; var field) {
                field = read_config_field(cf, name, field)
            }
        }
        return <- ret
    } static_elif (typeinfo is_string(type<TT>)) {
        return cf.value ?as _string ?? default<TT -const>
    } static_elif (typeinfo stripped_typename(type<TT>) == "bool") {
        return cf.value ?as _bool ?? default<TT -const>
    } static_elif (typeinfo is_enum(type<TT>)) {
        return from_CF(cf, type<TT>)
    } static_elif (typeinfo is_numeric(type<TT>)) {
        return from_CF(cf, type<TT>)
    } else {
        return from_CF(cf, type<TT>)
    }
}

[unsafe_outside_of_for]
def from_config(cf : StructField const?; t : type<auto(TT)>) : iterator<TT -const> {
    return <- generator<TT -const> {
        if (cf != null && cf.value is _array) {
            for (elem in cf.value as _array) {
                yield from_config_row(elem, type<TT>)
            }
        }
        return false
    }
}

[constant_expression(key=false)]
def read_config_field(cf : StructField const?; key : string; defv : auto(TT)) : TT {
    if (cf != null && cf.value is _object) {
        unsafe {
            let tab & = cf.value as _object
            let found = tab?[key]
            if (found != null) return from_config_row(*found, type<TT>)
        }
    }
    return defv
}