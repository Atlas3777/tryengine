options gen2
options no_global_variables = false
options indenting = 4
options strict_smart_pointers = true

module ConfigFormat shared public

//! Parser, serialization, and AST value manipulation for custom typed config format.
//! Format syntax rule: `name: TYPE = VALUE`

require daslib/strings_boost
require math

variant ConfigValue {
    //! Single config element supporting native daScript types for decs compatibility.
    _object   : table<string; StructField?>   //! Object / Struct instance
    _array    : array<StructField?>          //! Homogeneous array
    _string   : string                       //! string
    _double   : double                       //! f64
    _float    : float                        //! f32
    _int64    : int64                        //! i64
    _int      : int                          //! i32
    _int16    : int16                        //! i16
    _int8     : int8                         //! i8
    _uint64   : uint64                       //! u64
    _uint     : uint                         //! u32
    _uint16   : uint16                       //! u16
    _uint8    : uint8                        //! u8
    _bool     : bool                         //! bool
    _float2   : float2                       //! f32x2
    _float3   : float3                       //! f32x3
    _float4   : float4                       //! f32x4
    _int2     : int2                         //! i32x2
    _int3     : int3                         //! i32x3
    _int4     : int4                         //! i32x4
    _uint2    : uint2                        //! u32x2
    _uint3    : uint3                        //! u32x3
    _uint4    : uint4                        //! u32x4
    _float3x3 : float3x3                     //! f32m3x3
    _float3x4 : float3x4                     //! f32m3x4
    _float4x4 : float4x4                     //! f32m4x4
    _null     : void?                        //! Null
}

struct StructField {
    //! Field declaration containing name, explicit type annotation and value.
    name      : string
    typeName  : string
    value     : ConfigValue
}

struct ConfigReader {
    bytes  : array<uint8>
    cursor : int
    len    : int
    line   : int
    row    : int
}

def private make_reader(text : string implicit) : ConfigReader {
    var reader : ConfigReader
    reader.bytes |> reserve(length(text))
    for (ch in text) {
        push(reader.bytes, uint8(ch))
    }
    reader.len = length(reader.bytes)
    reader.line = 1
    reader.row = 1
    return <- reader
}

def private make_reader(text : array<uint8>) : ConfigReader {
    var reader : ConfigReader
    reader.bytes := text
    reader.len = length(reader.bytes)
    reader.line = 1
    reader.row = 1
    return <- reader
}

def private is_eof(reader : ConfigReader) : bool => reader.cursor >= reader.len

def private peek_char(reader : ConfigReader) : int {
    if (reader.cursor >= reader.len) {
        return 0
    }
    return int(reader.bytes[reader.cursor])
}

def private get_char(var reader : ConfigReader) : int {
    if (reader.cursor >= reader.len) {
        return 0
    }
    let ch = int(reader.bytes[reader.cursor])
    reader.cursor++
    if (ch == '\n') {
        reader.line++
        reader.row = 1
    } else {
        reader.row++
    }
    return ch
}

def private skip_whitespace_and_comments(var reader : ConfigReader) {
    while (!is_eof(reader)) {
        let ch = peek_char(reader)
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            get_char(reader)
        } elif (ch == '/') {
            if (reader.cursor + 1 < reader.len && reader.bytes[reader.cursor + 1] == '/'u8) {
                get_char(reader)
                get_char(reader)
                while (!is_eof(reader) && peek_char(reader) != '\n') {
                    get_char(reader)
                }
            } elif (reader.cursor + 1 < reader.len && reader.bytes[reader.cursor + 1] == '*'u8) {
                get_char(reader)
                get_char(reader)
                while (!is_eof(reader)) {
                    let c1 = get_char(reader)
                    if (c1 == '*' && peek_char(reader) == '/') {
                        get_char(reader)
                        break
                    }
                }
            } else {
                break
            }
        } else {
            break
        }
    }
}

def private expect_symbol(var reader : ConfigReader; sym : int; var error : string&) : bool {
    skip_whitespace_and_comments(reader)
    if (is_eof(reader)) {
        error = "unexpected EOS, expecting symbol `{to_char(sym)}` at {reader.line}:{reader.row}"
        return false
    }
    let ch = peek_char(reader)
    if (ch != sym) {
        error = "unexpected symbol `{to_char(ch)}`, expecting `{to_char(sym)}` at {reader.line}:{reader.row}"
        return false
    }
    get_char(reader)
    return true
}

def private read_identifier(var reader : ConfigReader; var error : string&) : string {
    skip_whitespace_and_comments(reader)
    if (is_eof(reader)) {
        error = "unexpected EOS expecting identifier at {reader.line}:{reader.row}"
        return ""
    }
    var str : array<uint8>
    while (!is_eof(reader)) {
        let ch = peek_char(reader)
        if (is_alpha(ch) || is_number(ch) || ch == '_') {
            push(str, uint8(get_char(reader)))
        } else {
            break
        }
    }
    if (length(str) == 0) {
        error = "expected identifier at {reader.line}:{reader.row}"
        return ""
    }
    return string(str)
}

def private read_quoted_string(var reader : ConfigReader; var error : string&) : string {
    skip_whitespace_and_comments(reader)
    if (!expect_symbol(reader, '"', error)) {
        return ""
    }
    var str : array<uint8>
    var str_err = false
    while (!is_eof(reader)) {
        var ch = get_char(reader)
        if (ch == '"') {
            return string(str)
        }
        if (ch == '\\') {
            if (!is_eof(reader)) {
                let esc = get_char(reader)
                if (esc == 'n') { ch = '\n'; }
                elif (esc == 'r') { ch = '\r'; }
                elif (esc == 't') { ch = '\t'; }
                elif (esc == '"') { ch = '"'; }
                elif (esc == '\\') { ch = '\\'; }
                else { ch = esc; }
            } else {
                str_err = true
                break
            }
        }
        push(str, uint8(ch))
    }
    error = str_err ? "string escape sequence exceeds text at {reader.line}:{reader.row}" : "string exceeds text at {reader.line}:{reader.row}"
    return ""
}

def private read_number_token(var reader : ConfigReader; var error : string&) : string {
    skip_whitespace_and_comments(reader)
    if (is_eof(reader)) {
        error = "unexpected EOS expecting number at {reader.line}:{reader.row}"
        return ""
    }
    var str : array<uint8>
    let first = peek_char(reader)
    if (first == '-' || first == '+' || is_number(first)) {
        push(str, uint8(get_char(reader)))
    } else {
        error = "expected number at {reader.line}:{reader.row}"
        return ""
    }
    while (!is_eof(reader)) {
        let ch = peek_char(reader)
        if (is_number(ch) || ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-') {
            push(str, uint8(get_char(reader)))
        } else {
            break
        }
    }
    return string(str)
}

def private parse_value_by_type(var reader : ConfigReader; typeName : string; var error : string&) : StructField? {
    skip_whitespace_and_comments(reader)

    // 1. Массивы
    if (ends_with(typeName, "[]")) {
        if (!expect_symbol(reader, '[', error)) return null
        var arr : array<StructField?>
        let elemType = slice(typeName, 0, length(typeName) - 2)

        while (true) {
            skip_whitespace_and_comments(reader)
            if (is_eof(reader)) {
                error = "unexpected EOS in array at {reader.line}:{reader.row}"
                return null
            }
            if (peek_char(reader) == ']') {
                get_char(reader)
                break
            }
            var element = parse_value_by_type(reader, elemType, error)
            if (element == null) return null
            push(arr, element)
        }
        return new StructField(value <- ConfigValue(_array <- arr))
    }

    // 2. Векторы
    if (typeName == "f32x2" || typeName == "f32x3" || typeName == "f32x4") {
        if (!expect_symbol(reader, '[', error)) return null
        var v : float4
        let count = typeName == "f32x2" ? 2 : (typeName == "f32x3" ? 3 : 4)
        for (i in range(count)) {
            let num_str = read_number_token(reader, error)
            if (!empty(error)) return null
            v[i] = float(num_str)
        }
        if (!expect_symbol(reader, ']', error)) return null

        if (typeName == "f32x2") return new StructField(value <- ConfigValue(_float2 = v.xy))
        if (typeName == "f32x3") return new StructField(value <- ConfigValue(_float3 = v.xyz))
        return new StructField(value <- ConfigValue(_float4 = v))
    }

    if (typeName == "i32x2" || typeName == "i32x3" || typeName == "i32x4") {
        if (!expect_symbol(reader, '[', error)) return null
        var v : int4
        let count = typeName == "i32x2" ? 2 : (typeName == "i32x3" ? 3 : 4)
        for (i in range(count)) {
            let num_str = read_number_token(reader, error)
            if (!empty(error)) return null
            v[i] = int(num_str)
        }
        if (!expect_symbol(reader, ']', error)) return null

        if (typeName == "i32x2") return new StructField(value <- ConfigValue(_int2 = v.xy))
        if (typeName == "i32x3") return new StructField(value <- ConfigValue(_int3 = v.xyz))
        return new StructField(value <- ConfigValue(_int4 = v))
    }

    if (typeName == "u32x2" || typeName == "u32x3" || typeName == "u32x4") {
        if (!expect_symbol(reader, '[', error)) return null
        var v : uint4
        let count = typeName == "u32x2" ? 2 : (typeName == "u32x3" ? 3 : 4)
        for (i in range(count)) {
            let num_str = read_number_token(reader, error)
            if (!empty(error)) return null
            v[i] = uint(num_str)
        }
        if (!expect_symbol(reader, ']', error)) return null

        if (typeName == "u32x2") return new StructField(value <- ConfigValue(_uint2 = v.xy))
        if (typeName == "u32x3") return new StructField(value <- ConfigValue(_uint3 = v.xyz))
        return new StructField(value <- ConfigValue(_uint4 = v))
    }

    // 3. Матрицы
    if (typeName == "f32m3x3") {
        if (!expect_symbol(reader, '[', error)) return null
        var mat : float3x3
        for (r in range(3)) {
            for (c in range(3)) {
                let num_str = read_number_token(reader, error)
                if (!empty(error)) return null
                mat[r][c] = float(num_str)
            }
        }
        if (!expect_symbol(reader, ']', error)) return null
        return new StructField(value <- ConfigValue(_float3x3 = mat))
    }

    if (typeName == "f32m3x4") {
        if (!expect_symbol(reader, '[', error)) return null
        var mat : float3x4
        for (r in range(3)) {
            for (c in range(4)) {
                let num_str = read_number_token(reader, error)
                if (!empty(error)) return null
                mat[r][c] = float(num_str)
            }
        }
        if (!expect_symbol(reader, ']', error)) return null
        return new StructField(value <- ConfigValue(_float3x4 = mat))
    }

    if (typeName == "f32m4x4") {
        if (!expect_symbol(reader, '[', error)) return null
        var mat : float4x4
        for (r in range(4)) {
            for (c in range(4)) {
                let num_str = read_number_token(reader, error)
                if (!empty(error)) return null
                mat[r][c] = float(num_str)
            }
        }
        if (!expect_symbol(reader, ']', error)) return null
        return new StructField(value <- ConfigValue(_float4x4 = mat))
    }

    // 4. Объекты / Структуры
    if (!is_eof(reader) && peek_char(reader) == '{') {
        get_char(reader)
        var tab : table<string; StructField?>
        while (true) {
            skip_whitespace_and_comments(reader)
            if (is_eof(reader)) {
                error = "unexpected EOS in object at {reader.line}:{reader.row}"
                return null
            }
            if (peek_char(reader) == '}') {
                get_char(reader)
                break
            }
            var fld = parse_field(reader, error)
            if (fld == null) return null
            let fldName = fld.name
            tab |> insert(fldName, fld)
        }
        return new StructField(value <- ConfigValue(_object <- tab))
    }

    // 5. Скаляры
    if (typeName == "string") {
        let s = read_quoted_string(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_string = s))
    }

    if (typeName == "bool") {
        let bname = read_identifier(reader, error)
        if (!empty(error)) return null
        if (bname == "true") return new StructField(value <- ConfigValue(_bool = true))
        if (bname == "false") return new StructField(value <- ConfigValue(_bool = false))
        error = "invalid bool value `{bname}` at {reader.line}:{reader.row}"
        return null
    }

    if (typeName == "u64" || typeName == "uint64") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_uint64 = uint64(num_str)))
    }
    if (typeName == "u32" || typeName == "uint") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_uint = uint(num_str)))
    }
    if (typeName == "u16" || typeName == "uint16") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_uint16 = uint16(num_str)))
    }
    if (typeName == "u8" || typeName == "uint8") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_uint8 = uint8(num_str)))
    }

    if (typeName == "i64" || typeName == "int64") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_int64 = int64(num_str)))
    }
    if (typeName == "i32" || typeName == "int") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_int = int(num_str)))
    }
    if (typeName == "i16" || typeName == "int16") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_int16 = int16(num_str)))
    }
    if (typeName == "i8" || typeName == "int8") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_int8 = int8(num_str)))
    }

    if (typeName == "f32" || typeName == "float") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_float = float(num_str)))
    }
    if (typeName == "f64" || typeName == "double") {
        let num_str = read_number_token(reader, error)
        if (!empty(error)) return null
        return new StructField(value <- ConfigValue(_double = double(num_str)))
    }

    error = "unknown type or unexpected value for type `{typeName}` at {reader.line}:{reader.row}"
    return null
}

def private parse_field(var reader : ConfigReader; var error : string&) : StructField? {
    skip_whitespace_and_comments(reader)
    if (is_eof(reader)) return null
    if (peek_char(reader) == '}') return null

    let fieldName = read_identifier(reader, error)
    if (!empty(error)) return null

    if (!expect_symbol(reader, ':', error)) return null

    var typeName = read_identifier(reader, error)
    if (!empty(error)) return null

    skip_whitespace_and_comments(reader)
    while (!is_eof(reader) && peek_char(reader) == '[') {
        get_char(reader)
        if (!expect_symbol(reader, ']', error)) return null
        typeName += "[]"
        skip_whitespace_and_comments(reader)
    }

    if (!expect_symbol(reader, '=', error)) return null

    var fieldVal = parse_value_by_type(reader, typeName, error)
    if (fieldVal == null) return null

    return new StructField(name = fieldName, typeName = typeName, value <- fieldVal.value)
}

def read_config(text : string implicit; var error : string&) : StructField? {
    error = ""
    var reader = make_reader(text)
    var rootObj : table<string; StructField?>

    while (!is_eof(reader)) {
        skip_whitespace_and_comments(reader)
        if (is_eof(reader)) break
        var fld = parse_field(reader, error)
        if (fld == null) {
            if (!empty(error)) return null
            break
        }
        let fldName = fld.name
        rootObj |> insert(fldName, fld)
    }

    return new StructField(name = "root", typeName = "Object", value <- ConfigValue(_object <- rootObj))
}

def read_config(text : array<uint8>; var error : string&) : StructField? {
    error = ""
    var reader = make_reader(text)
    var rootObj : table<string; StructField?>

    while (!is_eof(reader)) {
        skip_whitespace_and_comments(reader)
        if (is_eof(reader)) break
        var fld = parse_field(reader, error)
        if (fld == null) {
            if (!empty(error)) return null
            break
        }
        let fldName = fld.name
        rootObj |> insert(fldName, fld)
    }

    return new StructField(name = "root", typeName = "Object", value <- ConfigValue(_object <- rootObj))
}

def private write_field(var writer : StringBuilderWriter; field : StructField?; depth : int) {
    if (field == null) return

    write_chars(writer, '\t', depth)
    if (length(field.name) > 0) {
        write(writer, field.name)
        write(writer, ": ")
        write(writer, field.typeName)
        write(writer, " = ")
    }

    unsafe {
        let val & = field.value
        if (val is _string) {
            write(writer, "\"")
            write(writer, val as _string)
            write(writer, "\"")
        } elif (val is _double) {
            write(writer, val as _double)
        } elif (val is _float) {
            write(writer, val as _float)
        } elif (val is _int) {
            write(writer, val as _int)
        } elif (val is _int64) {
            write(writer, val as _int64)
        } elif (val is _int16) {
            write(writer, val as _int16)
        } elif (val is _int8) {
            write(writer, int(val as _int8))
        } elif (val is _uint) {
            write(writer, "{val as _uint:d}")
        } elif (val is _uint64) {
            write(writer, "{val as _uint64:d}")
        } elif (val is _uint16) {
            write(writer, "{val as _uint16:d}")
        } elif (val is _uint8) {
            write(writer, "{val as _uint8:d}")
        } elif (val is _bool) {
            write(writer, (val as _bool) ? "true" : "false")
        } elif (val is _float2) {
            let v = val as _float2
            write(writer, "[{v.x} {v.y}]")
        } elif (val is _float3) {
            let v = val as _float3
            write(writer, "[{v.x} {v.y} {v.z}]")
        } elif (val is _float4) {
            let v = val as _float4
            write(writer, "[{v.x} {v.y} {v.z} {v.w}]")
        } elif (val is _int2) {
            let v = val as _int2
            write(writer, "[{v.x} {v.y}]")
        } elif (val is _int3) {
            let v = val as _int3
            write(writer, "[{v.x} {v.y} {v.z}]")
        } elif (val is _int4) {
            let v = val as _int4
            write(writer, "[{v.x} {v.y} {v.z} {v.w}]")
        } elif (val is _uint2) {
            let v = val as _uint2
            write(writer, "[{v.x} {v.y}]")
        } elif (val is _uint3) {
            let v = val as _uint3
            write(writer, "[{v.x} {v.y} {v.z}]")
        } elif (val is _uint4) {
            let v = val as _uint4
            write(writer, "[{v.x} {v.y} {v.z} {v.w}]")
        } elif (val is _float3x3) {
            let m = val as _float3x3
            write(writer, "[{m[0].x} {m[0].y} {m[0].z} {m[1].x} {m[1].y} {m[1].z} {m[2].x} {m[2].y} {m[2].z}]")
        } elif (val is _float3x4) {
            let m = val as _float3x4
            write(writer, "[{m[0].x} {m[0].y} {m[0].z} {m[1].x} {m[1].y} {m[1].z} {m[2].x} {m[2].y} {m[2].z} {m[3].x} {m[3].y} {m[3].z}]")
        } elif (val is _float4x4) {
            let m = val as _float4x4
            write(writer, "[{m[0].x} {m[0].y} {m[0].z} {m[0].w} {m[1].x} {m[1].y} {m[1].z} {m[1].w} {m[2].x} {m[2].y} {m[2].z} {m[2].w} {m[3].x} {m[3].y} {m[3].z} {m[3].w}]")
        } elif (val is _array) {
            let arr & = val as _array
            write(writer, "[\n")
            for (elem in arr) {
                write_field(writer, elem, depth + 1)
                write(writer, "\n")
            }
            write_chars(writer, '\t', depth)
            write(writer, "]")
        } elif (val is _object) {
            let obj & = val as _object
            write(writer, "\{\n")
            for (fldV in values(obj)) {
                write_field(writer, fldV, depth + 1)
                write(writer, "\n")
            }
            write_chars(writer, '\t', depth)
            write(writer, "}")
        }
    }
}

def write_config(val : StructField?) : string {
    return build_string() $(var writer) {
        if (val != null && val.value is _object) {
            unsafe {
                let obj & = val.value as _object
                for (fldV in values(obj)) {
                    write_field(writer, fldV, 0)
                    write(writer, "\n")
                }
            }
        }
    }
}