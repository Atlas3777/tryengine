options gen2

module Scene shared

require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost

require daslib/decs
require daslib/decs_boost
require daslib/coroutines
require daslib/async_boost
require daslib/safe_addr

require tryAsyncFile
require strings

var g_async_tasks : Coroutines

def finalize(var file_handle : FileHandle&) {
    finalize_file_handle(file_handle)
}

def start_task(var task : iterator<bool>) {
    g_async_tasks |> emplace <| task
}

[export]
def tick_async_tasks() {
    var i = length(g_async_tasks) - 1
    while (i >= 0) {
        var is_running = false
        if (!next(g_async_tasks[i], is_running) || !is_running) {
            g_async_tasks |> erase(i)
        }
        i--
    }
}

[async]
def ReadFile(path : string; offset : uint64; size : uint) : array<uint8> {
    var handle <- ReadChunkAsync(path, offset, size)
    while (!handle.IsReady) {
        await_next_frame()
    }
    yield <- GetDataSpan(handle)
}

[async]
def GetFileSize(path : string) : uint64 {
    var handle <- GetStatAsync(path)
    while (!handle.IsReady) {
        await_next_frame()
    }
    let size = handle.GetFileSize
    delete handle
    yield size
}

struct Scene {
    requireMeshs : array<uint64>
    requireMaterials : array<uint64>
    entities : array<StructField?>
}

def save_scene_to_config() : string {
    var entities_list : array<StructField?>

    for (arch in decsState.allArchetypes) {
        for (i in range64(arch.size)) {
            var entity_obj : table<string; StructField?>

            for (comp in arch.components) {
                var cf_val = component_to_config_value(comp, i)
                if (cf_val != null) {
                    cf_val.name = comp.name
                    entity_obj[comp.name] = cf_val
                }
            }

            var entity_cf = CF(entity_obj)
            entity_cf.typeName = "Entity"
            entities_list |> push(entity_cf)
        }
    }

    var scene : Scene

    var exist = $(guid : uint64, arr : array<uint64>?#) : bool {
        for (g in *arr){
            if (guid == g)
                return true
        }

        return false
    }

    query()$(let meshGuid : uint64, let materialGuid : uint64){
        if (!exist(meshGuid, safe_addr(scene.requireMeshs))){
            scene.requireMeshs |> push(meshGuid)
        }
        if (!exist(materialGuid, safe_addr(scene.requireMaterials))){
            scene.requireMaterials |> push(materialGuid)
        }
    }

    scene.entities <- entities_list

    return write_config(CF(scene))

    // var root_obj : table<string; StructField?>
    // var entities_cf = CF(entities_list)
    // entities_cf.name = "entities"
    // entities_cf.typeName = "Entity[]"
    // root_obj["entities"] = entities_cf

    // return write_config(CF(root_obj))
}


// ---------------------------------------------------------------------
// Сериализация компонентов DECS в StructField?
// ---------------------------------------------------------------------
def private component_to_config_value(comp : Component; index : int64) : StructField? {
    let offset = index * int64(comp.stride)
    unsafe {
        let ptr = addr(comp.data[offset])

        // Сравниваем полное имя типа через встроенную рефлексию
        if (comp.info.fullName == typeinfo fulltypename(type<EntityId>)) {
            var cf = CF(*reinterpret<EntityId?>(ptr))
            cf.typeName = "EntityId"
            return cf
        } elif (comp.info.basicType == Type.tFloat) {
            return CF(*reinterpret<float?>(ptr))
        } elif (comp.info.basicType == Type.tFloat2) {
            return CF(*reinterpret<float2?>(ptr))
        } elif (comp.info.basicType == Type.tFloat3) {
            return CF(*reinterpret<float3?>(ptr))
        } elif (comp.info.basicType == Type.tFloat4) {
            return CF(*reinterpret<float4?>(ptr))
        } elif (comp.info.basicType == Type.tInt) {
            return CF(*reinterpret<int?>(ptr))
        } elif (comp.info.basicType == Type.tInt2) {
            return CF(*reinterpret<int2?>(ptr))
        } elif (comp.info.basicType == Type.tInt3) {
            return CF(*reinterpret<int3?>(ptr))
        } elif (comp.info.basicType == Type.tInt4) {
            return CF(*reinterpret<int4?>(ptr))
        } elif (comp.info.basicType == Type.tUInt) {
            return CF(*reinterpret<uint?>(ptr))
        } elif (comp.info.basicType == Type.tUInt64) {
            return CF(*reinterpret<uint64?>(ptr))
        } elif (comp.info.basicType == Type.tInt64) {
            return CF(*reinterpret<int64?>(ptr))
        } elif (comp.info.basicType == Type.tDouble) {
            return CF(*reinterpret<double?>(ptr))
        } elif (comp.info.basicType == Type.tBool) {
            return CF(*reinterpret<bool?>(ptr))
        } elif (comp.info.basicType == Type.tString) {
            return CF(*reinterpret<string?>(ptr))
        }
    }
    return null
}

def private parse_and_apply_field(var cmp : ComponentMap;
                                  entity_tag : string;
                                  field_name : string;
                                  cf : StructField?;
                                  renames : table<string; string>;
                                  var out_warnings : int&) : bool {
    var target_name = field_name

    if (renames |> key_exists(field_name)) {
        target_name = renames?[field_name] ?? field_name
        print("[SCENE MIGRATION] Entity '{entity_tag}': Field '{field_name}' renamed -> '{target_name}'.\n")
        out_warnings++
    }

    if (cf == null || cf.value is _null) {
        print("[SCENE WARNING] Entity '{entity_tag}': Field '{target_name}' is null. Skipped.\n")
        out_warnings++
        return false
    }

    if (decsState.componentTypeCheck |> key_exists(target_name)) {
        unsafe {
            let info = decsState.componentTypeCheck?[target_name]
            if (info.fullName == typeinfo fulltypename(type<EntityId>)) {
                cmp |> set(target_name, from_CF(cf, type<EntityId>))
                return true
            } elif (info.basicType == Type.tFloat) {
                cmp |> set(target_name, from_CF(cf, type<float>))
                return true
            } elif (info.basicType == Type.tFloat2) {
                cmp |> set(target_name, from_CF(cf, type<float2>))
                return true
            } elif (info.basicType == Type.tFloat3) {
                cmp |> set(target_name, from_CF(cf, type<float3>))
                return true
            } elif (info.basicType == Type.tFloat4) {
                cmp |> set(target_name, from_CF(cf, type<float4>))
                return true
            } elif (info.basicType == Type.tInt) {
                cmp |> set(target_name, from_CF(cf, type<int>))
                return true
            } elif (info.basicType == Type.tInt2) {
                cmp |> set(target_name, from_CF(cf, type<int2>))
                return true
            } elif (info.basicType == Type.tInt3) {
                cmp |> set(target_name, from_CF(cf, type<int3>))
                return true
            } elif (info.basicType == Type.tInt4) {
                cmp |> set(target_name, from_CF(cf, type<int4>))
                return true
            } elif (info.basicType == Type.tUInt) {
                cmp |> set(target_name, from_CF(cf, type<uint>))
                return true
            } elif (info.basicType == Type.tUInt64) {
                cmp |> set(target_name, from_CF(cf, type<uint64>))
                return true
            } elif (info.basicType == Type.tInt64) {
                cmp |> set(target_name, from_CF(cf, type<int64>))
                return true
            } elif (info.basicType == Type.tBool) {
                cmp |> set(target_name, from_CF(cf, type<bool>))
                return true
            } elif (info.basicType == Type.tString) {
                cmp |> set(target_name, from_CF(cf, type<string>))
                return true
            }
        }
    }

    unsafe {
        let val & = cf.value
        if (val is _float) {
            cmp |> set(target_name, val as _float)
            return true
        } elif (val is _float2) {
            cmp |> set(target_name, val as _float2)
            return true
        } elif (val is _float3) {
            cmp |> set(target_name, val as _float3)
            return true
        } elif (val is _float4) {
            cmp |> set(target_name, val as _float4)
            return true
        } elif (val is _int) {
            cmp |> set(target_name, val as _int)
            return true
        } elif (val is _int2) {
            cmp |> set(target_name, val as _int2)
            return true
        } elif (val is _int3) {
            cmp |> set(target_name, val as _int3)
            return true
        } elif (val is _int4) {
            cmp |> set(target_name, val as _int4)
            return true
        } elif (val is _uint) {
            cmp |> set(target_name, val as _uint)
            return true
        } elif (val is _uint64) {
            cmp |> set(target_name, val as _uint64)
            return true
        } elif (val is _int64) {
            cmp |> set(target_name, val as _int64)
            return true
        } elif (val is _bool) {
            cmp |> set(target_name, val as _bool)
            return true
        } elif (val is _string) {
            cmp |> set(target_name, val as _string)
            return true
        } elif (val is _object) {
            cmp |> set(target_name, from_CF(cf, type<EntityId>))
            return true
        }

        print("[SCENE WARNING] Entity '{entity_tag}': Field '{target_name}' has unsupported ConfigFormat type. Skipped.\n")
        out_warnings++
        return false
    }
}

def load_scene_from_config(config_text : string; renames : table<string; string>) : bool {
    var error : string
    var root = read_config(config_text, error)

    // Проверяем текстовую ошибку парсера, так как root никогда не равен null
    if (length(error) > 0 || root == null) {
        print("[SCENE ERROR] Failed to parse Config format: {error}\n")
        return false
    }

    if (!(root.value is _object)) {
        print("[SCENE ERROR] Config root is not an object.\n")
        return false
    }

    unsafe {
        let root_obj & = root.value as _object
        if (!root_obj |> key_exists("entities")) {
            print("[SCENE ERROR] Config root missing 'entities' field.\n")
            return false
        }

        let entities_cf = root_obj?["entities"] ?? null
        if (entities_cf == null || !(entities_cf.value is _array)) {
            print("[SCENE ERROR] 'entities' field is not an array.\n")
            return false
        }

        let entities_arr & = entities_cf.value as _array
        let ent_count = length(entities_arr)
        var total_loaded = 0
        var warnings_count = 0

        print("\n==================================================\n")
        print(" [SCENE LOAD] Starting runtime validation & load...\n")
        print("==================================================\n")

        decs::restart()

        var renames_captured := renames

        for (ent_idx in range(ent_count)) {
            let ent_cf = entities_arr[ent_idx]
            if (ent_cf == null || !(ent_cf.value is _object)) {
                print("[SCENE WARNING] Element #{ent_idx} is not a valid Config object. Skipped.\n")
                warnings_count++
                continue
            }

            let ent_obj & = ent_cf.value as _object

            var entity_tag = "Entity_{ent_idx}"
            if (ent_obj |> key_exists("tag")) {
                let tag_cf = ent_obj?["tag"] ?? null
                if (tag_cf != null && tag_cf.value is _string) {
                    entity_tag = from_CF(tag_cf, type<string>)
                }
            }

            create_entity() @(eid : EntityId; var cmp : ComponentMap) {
                unsafe {
                    let cur_ent_obj & = ent_cf.value as _object

                    for (field_name in keys(cur_ent_obj)) {
                        let cf = cur_ent_obj?[field_name] ?? null
                        parse_and_apply_field(cmp, entity_tag, field_name, cf, renames_captured, warnings_count)
                    }
                }
            }

            total_loaded++
        }

        commit()

        print("--------------------------------------------------\n")
        print(" [SCENE LOAD COMPLETED]\n")
        print(" Total Entities Loaded: {total_loaded}\n")
        print(" Warnings / Skipped fields: {warnings_count}\n")
        print("==================================================\n\n")
    }

    return true
}

def load_scene_from_config(config_text : string) : bool {
    var renames : table<string; string>
    return load_scene_from_config(config_text, renames)
}

[async]
def SaveSceneAsync(path : string) : void {
    let config_text = save_scene_to_config()
    let str_len = length(config_text)
    var data : array<uint8>
    data |> resize(str_len)

    peek_data(config_text) $(bytes) {
        for (i in range(str_len)) {
            data[i] = bytes[i]
        }
    }

    var handle <- WriteChunkAsync(path, data, 0ul)
    while (!handle.IsReady) {
        await_next_frame()
    }
    delete handle
    print("[SCENE] Scene saved successfully to '{path}'.\n")
}

[async]
def LoadSceneAsync(path : string) : void {
    let file_size <- await(GetFileSize(path))
    if (file_size == 0ul) {
        print("[SCENE ERROR] File '{path}' does not exist or is empty.\n")
        return false
    }

    let bytes <- await(ReadFile(path, 0ul, uint(file_size)))
    let config_text = build_string() <| $(writer) {
        for (b in bytes) {
            writer |> write_char(int(b))
        }
    }

    // print("[DEBUG I/O] Path: '{path}', Stat size: {file_size}, Loaded bytes: {length(bytes)}\n")
    // print("[DEBUG CONTENT START]\n{config_text}\n[DEBUG CONTENT END]\n")

    load_scene_from_config(config_text)
}