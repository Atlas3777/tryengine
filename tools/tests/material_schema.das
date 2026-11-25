options gen2
options relaxed_assign = false

require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require strings

//! The material schema that C++ writes (`BuildMaterialSchemaConfig`) has to be readable by
//! the editor's ConfigFormat parser. `attributes` is the shader's mesh-attribute mask; the
//! material picker compares it with the mesh. This is a pure parser check: no GPU, no Slang.
let kSchema = "params: MaterialParam[] = [\n\t\{\n\t\tname: string = \"albedo_map\"\n\t\ttype: string = \"u64\"\n\t\tkind: string = \"texture\"\n\t\tbinding: u32 = 1\n\t\}\n]\nattributes: u32 = 5\n"

[export]
def main() : int {
    var ok = true

    var error = ""
    var root <- read_config(kSchema, error)
    if (!empty(error) || root == null || !(root.value is _object)) {
        print("FAIL: material schema did not parse: {error}\n")
        return 1
    }

    var object <- root.value as _object
    let params = object?["params"] ?? null
    if (params == null || !(params.value is _array)) {
        print("FAIL: params is not an array\n")
        ok = false
    } else {
        assume elements = params.value as _array
        ok = ok && length(elements) == 1
    }

    let attributes = object?["attributes"] ?? null
    if (attributes == null || !(attributes.value is _uint)) {
        print("FAIL: attributes is not a u32\n")
        ok = false
    } else {
        let mask = attributes.value as _uint
        ok = ok && mask == 5u
        print("attributes: {mask:d}\n")
    }

    if (!ok) {
        return 1
    }
    return 0
}
