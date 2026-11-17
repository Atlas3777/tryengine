options gen2
options relaxed_assign = false

module AssetKind shared public

// Asset kind is declared on the wrapper type, not in a global registry:
//
//   [asset(kind = "data", title = "Material", ext = ".mat")]
//   struct MaterialRef {
//       path : string
//   }
//
// `kind` is the storage domain (engine/game/content/data) — not a subfolder. `ext` filters
// the whole domain tree, so scene/prefab pickers see every `.scene`/`.prefab` anywhere
// under content. `title` is only the popup caption; it is optional and defaults to the
// extension. The annotation is engine-side and inert: it only stores metadata in a table
// so the editor can resolve a wrapper by its type name. No editor types are required here.

require daslib/ast
require daslib/ast_boost
require daslib/templates_boost
require daslib/rtti
require strings
require daslib/strings_boost

struct AssetTypeInfo {
    has   : bool
    kind  : string
    title : string
    ext   : string
}

var public g_asset_type_infos : table<string; AssetTypeInfo>

// The same short-name normalization as tryengine/Serializable: type names arrive either
// from a field schema or from DECS, and both may carry the module qualifier.
def public NormalizeAssetTypeName(name : string) : string {
    let idx = last_index_of(name, "::")
    return idx >= 0 ? slice(name, idx + 2, length(name)) : name
}

def public GetAssetTypeInfo(type_name : string; var out : AssetTypeInfo) : bool {
    let key = NormalizeAssetTypeName(type_name)
    if (!key_exists(g_asset_type_infos, key)) {
        return false
    }
    out = g_asset_type_infos[key]
    return true
}

// Compile-time view of the same annotation, used by the type-parameterized query cache.
def public AssetInfoOf(t : type<auto(TT)>) : AssetTypeInfo {
    var info : AssetTypeInfo
    static_if (typeinfo struct_safe_has_annotation < asset > (type<TT>)) {
        info.has = true
        static_if (typeinfo struct_safe_has_annotation_argument < asset; kind > (type<TT>)) {
            info.kind = typeinfo struct_get_annotation_argument<asset; kind>(type<TT>)
        }
        static_if (typeinfo struct_safe_has_annotation_argument < asset; title > (type<TT>)) {
            info.title = typeinfo struct_get_annotation_argument<asset; title>(type<TT>)
        }
        static_if (typeinfo struct_safe_has_annotation_argument < asset; ext > (type<TT>)) {
            info.ext = typeinfo struct_get_annotation_argument<asset; ext>(type<TT>)
        }
    }
    return info
}

[structure_macro(name="asset")]
class AssetAnnotation : AstStructureAnnotation {
    def override apply(var st : StructurePtr; var group : ModuleGroup;
                       var args : AnnotationArgumentList; var errors : das_string) : bool {
        let type_name = st.name.string()
        let kind_arg = args |> find_arg("kind")
        let ext_arg = args |> find_arg("ext")
        let title_arg = args |> find_arg("title")

        if (!(kind_arg is tString)) {
            errors := "asset: kind must be a string"
            return false
        }
        if (!(ext_arg is tString)) {
            errors := "asset: ext must be a string"
            return false
        }
        if (!(title_arg is tString) && !(title_arg is nothing)) {
            errors := "asset: title must be a string"
            return false
        }

        let kind = kind_arg as tString
        let ext = ext_arg as tString
        let title = title_arg is tString ? (title_arg as tString) : ""
        if (kind != "engine" && kind != "game" && kind != "content" && kind != "data") {
            errors := "asset: kind must be a storage domain: engine, game, content or data"
            return false
        }
        if (empty(ext) || !starts_with(ext, ".")) {
            errors := "asset: ext must start with '.', e.g. \".mat\""
            return false
        }

        // The inspector addresses the value through a field named `path`; keep every
        // wrapper the same shape so draw_asset_ref does not need a per-kind layout.
        var field_count = 0
        var has_path = false
        for (fld in st.fields) {
            field_count++
            if (fld.name == "path") {
                has_path = true
            }
        }
        if (field_count != 1 || !has_path) {
            errors := "asset: wrapper must have exactly one field named 'path'"
            return false
        }

        let fn_init_name = "register_asset_{type_name}"
        var fnInit <- qmacro_function(fn_init_name) $() {
            g_asset_type_infos[$v(type_name)] = AssetTypeInfo(has = true, kind = $v(kind), title = $v(title), ext = $v(ext))
        }
        fnInit.flags.init = true
        if (!(compiling_module() |> add_function(fnInit))) {
            errors := "asset: failed to add {fn_init_name}"
            return false
        }
        return true
    }
}
