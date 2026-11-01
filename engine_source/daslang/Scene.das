options gen2

module Scene shared public

require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryResources

require daslib/decs
require daslib/decs_boost
require daslib/safe_addr
require daslib/async_boost
require daslib/array_boost
require daslib/linq

require daslib/rtti
require strings
require tryengine/FileReader
require tryengine/Types
require tryengine/Serializable
require daslib/strings_boost


struct Scene {
    requireMeshs : array<uint64>
    requireMaterials : array<uint64>
}

struct LoadedSceneContext {
    scene_name : string // берется из имени файла
    scene_path : string
}

var scene_context : LoadedSceneContext
var active_scene : Scene

[async]
def LoadScene(name, path : string) : void {
    UnloadActiveScene()

    scene_context.scene_name = name
    scene_context.scene_path = path

    await(LoadSceneAsync(path))
}

def UnloadActiveScene(){
    for (mat in active_scene.requireMaterials){
        ReleaseMaterialResource(mat)
    }
    for (mesh in active_scene.requireMeshs){
        ReleaseMeshResource(mesh)
    }
    active_scene = Scene()
    decs::restart()
}

def SetScenePath(name, path : string) {
    scene_context.scene_name = name
    scene_context.scene_path = path
}

def GetScenePath() : string {
    return scene_context.scene_path
}

[async]
def CreateScene(name, path : string) : void {
    UnloadActiveScene()
    CreateCamera()

    scene_context.scene_name = name
    scene_context.scene_path = path
}

[async]
def SaveCurrentScene() : void {
    await(SaveSceneAsync(scene_context.scene_path))
}

[async]
def SaveSceneAsync(path : string) : void {
    let config_text = save_scene_to_config()
    let str_len = length(config_text)

    var data <- config_text.to_bytes()

    var handle <- WriteChunkAsync(path, data, 0ul)
    while (!handle.IsReady) {
        await_next_frame()
    }

    delete handle
    print("[SCENE] Scene saved successfully to '{path}'.\n")
}

[async]
def LoadSceneAsync(path : string) : void {
    let file_size : uint64 <- await(FileReader::GetFileSize(path))
    if (file_size == 0ul) {
        print("[SCENE ERROR] File '{path}' does not exist or is empty.\n")
        return false
    }

    let bytes : array<uint8> <- await(FileReader::ReadFile(path, 0ul, uint(file_size)))
    let config_text = string(bytes)

    print("[DEBUG I/O] Path: '{path}', Stat size: {file_size}, Loaded bytes: {length(bytes)}\n")
    // print("[DEBUG CONTENT START]\n{config_text}\n[DEBUG CONTENT END]\n")

    load_scene_from_config(config_text)
}

[DecsSerializable]
struct TestSer {
    name : string
    somee : uint64
    //pp : CastPlayer
    name2 : string
}

struct CastPlayer{
    hp : int
    speed : float
}

def private CreateCamera{
        create_entity() @(eid, cmp){
        cmp.tag := "Camera"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, 0.0f, 2.0f),
            rotation = float4(0f, 0f, 0f, 1f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        apply_decs_template(cmp, Camera(
            fov = 60.0f,
            near_plane = 0.01f,
            far_plane = 100.0f
        ))
        apply_decs_template(cmp, Relationship())
        cmp |> set("TestSerRRR", TestSer(name = "tet", somee = 324ul, name2 = "HAHHA2"))
    }
}

def private parse_uint64_array(cf : StructField?) : array<uint64> {
    var res : array<uint64>
    if (cf != null && cf.value is _array) {
        assume arr  = cf.value as _array
        for (elem in arr) {
            if (elem != null) {
                if (elem.value is _uint64) {
                    res |> push(elem.value as _uint64)
                } elif (elem.value is _int64) {
                    res |> push(uint64(elem.value as _int64))
                } elif (elem.value is _uint) {
                    res |> push(uint64(elem.value as _uint))
                } elif (elem.value is _int) {
                    res |> push(uint64(elem.value as _int))
                }
            }
        }
    }
    return <- res
}

struct private SaveSceneDTO {
    requireMeshs : array<uint64>
    requireMaterials : array<uint64>
    entities : array<StructField?>
}

def private save_scene_to_config() : string {
    var entities_list : array<StructField?>

    // 1. Собираем сущности из DECS
    for (arch in decsState.allArchetypes) {
        for (i in range64(arch.size)) {
            var entity_obj : table<string; StructField?>

            for (comp in arch.components) {
                if (comp.info.basicType == Type.tPointer)
                    continue

                var cf_val = ComponentToCF(comp, i)
                cf_val.name = comp.name
                entity_obj[comp.name] = cf_val
            }

            var entity_cf = CF(entity_obj)
            entity_cf.typeName = "Entity"
            entities_list |> push(entity_cf)
        }
    }

    active_scene.requireMeshs |> clear()
    active_scene.requireMaterials |> clear()

    query() $(meshGuid : uint64) {
        if (!active_scene.requireMeshs.contains(meshGuid))
            active_scene.requireMeshs |> push(meshGuid)
    }
    query() $(materialGuid : uint64) {
        if (!active_scene.requireMaterials.contains(materialGuid))
            active_scene.requireMaterials |> push(materialGuid)
    }

    // 3. Формируем временный DTO для рефлексии CF()
    var dto = SaveSceneDTO(
        requireMeshs := active_scene.requireMeshs,
        requireMaterials := active_scene.requireMaterials,
        entities <- entities_list
    )

    return write_config(CF(dto))
}


def private ComponentToCF(comp : Component&; index : int64) : StructField? {
    let offset = index * int64(comp.stride)
    unsafe {
        let ptr = addr(comp.data[offset])
        if (comp.info.basicType == Type.tStructure){
            print("Пытаемся {comp.name} структуру сериализовать \n")
            return SerializeStruct(reinterpret<void?>(ptr), comp.info.mkTypeInfo().firstType.structType)
        } elif (comp.info.basicType == Type.none){
            print("comp.info.basicType == Type.none")
            return CF("Type.none")
        } else {
            return SerializeBase(ptr, comp.info.basicType)
        }
    }
}

def private SerializeStruct(ptr : void?; candidate : StructInfo const?) : StructField? {
    if (ptr == null){
        print("ptr govno\n")
        return null
    }
    if (candidate == null){
        print("candidate govno\n")
        return null
    }

    unsafe {
        print("start STRUCT \n")

        var map : table<string; StructField?>

        if (candidate.fields != null) {
            var pp = candidate.fields
            for (c in range(candidate.count)) {
                let current = *pp
                pp++
                if (current == null)
                    continue

                let fieldPtr = reinterpret<void?>(reinterpret<uint64>(ptr) + uint64(current.offset))
                var fieldNode = current.basicType == Type.tStructure ? SerializeStruct(fieldPtr, current.structType) : SerializeBase(fieldPtr, current.basicType)
                // print("c={c} ptr={intptr(current):d} offset={current.offset:d} bt={current.basicType}\n")

                if (fieldNode != null) {
                    fieldNode.name = current.name
                    map |> insert(current.name, fieldNode)
                }
            }

        }
        print("END struct\n")
        return CF(map, candidate.name)
    }

    return null
}

def private SerializeBase(p : void?; t : Type) : StructField? {
    unsafe{
        if (t == Type.tFloat){
            return CF(*reinterpret<float?>(p))
        } elif (t == Type.tFloat2) {
            return CF(*reinterpret<float2?>(p))
        } elif (t == Type.tFloat3) {
            return CF(*reinterpret<float3?>(p))
        } elif (t == Type.tFloat4) {
            return CF(*reinterpret<float4?>(p))
        } elif (t == Type.tInt){
            return CF(*reinterpret<int?>(p))
        } elif (t == Type.tInt2) {
            return CF(*reinterpret<int2?>(p))
        } elif (t == Type.tInt3) {
            return CF(*reinterpret<int3?>(p))
        } elif (t == Type.tInt4) {
            return CF(*reinterpret<int4?>(p))
        } elif (t == Type.tUInt){
            return CF(*reinterpret<uint?>(p))
        } elif (t == Type.tUInt2) {
            return CF(*reinterpret<uint2?>(p))
        } elif (t == Type.tUInt3) {
            return CF(*reinterpret<uint3?>(p))
        } elif (t == Type.tUInt4) {
            return CF(*reinterpret<uint4?>(p))
        } elif (t == Type.tInt64){
            return CF(*reinterpret<int64?>(p))
        } elif (t == Type.tUInt64){
            return CF(*reinterpret<uint64?>(p))
        } elif (t == Type.tString){
            return CF(*reinterpret<string?>(p))
        } elif (t == Type.tDouble) {
            return CF(*reinterpret<double?>(p))
        } elif (t == Type.tBool) {
            return CF(*reinterpret<bool?>(p))
        } elif (t == Type.tString) {
            return CF(*reinterpret<string?>(p))
        } elif (t == Type.tInt8){
            return CF(*reinterpret<int8?>(p))
        } elif (t == Type.tUInt8){
            return CF(*reinterpret<uint8?>(p))
        } elif (t == Type.tUInt16){
            return CF(*reinterpret<uint16?>(p))
        } elif (t == Type.tInt16){
            return CF(*reinterpret<int16?>(p))
        } elif (t == Type.none){
            print("current.basicType NONE \n")
        }
        print("не смогли обработать {t}\n")
        return CF("fallback")
    }
}

def private parse_and_apply_field(var cmp : ComponentMap;
                                  entity_tag : string;
                                  field_name : string;
                                  cf : StructField?;
                                  renames : table<string; string>;
                                  var out_warnings : int&,
                                  type_name : string) : bool {
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
        let info = decsState.componentTypeCheck |> get_value(target_name)
        if (info.basicType == Type.tFloat) {
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
        } elif (info.basicType == Type.tStructure) {
            if (!DecsAddComponent(cmp, type_name, target_name, cf))
                print("Type is not registered {type_name}\n")

            return true
        }
    }

    // print("Loading CMP dont contains in DECS {field_name} \n")

    assume val = cf.value
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
        if (!DecsAddComponent(cmp, type_name, target_name, cf))
            print("Type is not registered: {type_name}\n")

        return true
    }

    print("[SCENE WARNING] Entity '{entity_tag}': Field '{target_name}' has unsupported ConfigFormat type. Skipped.\n")
    out_warnings++
    return false
}

def load_scene_from_config(config_text : string; renames : table<string; string>) : bool {
    var error : string
    var root = read_config(config_text, error)

    if (length(error) > 0 || root == null) {
        print("[SCENE ERROR] Failed to parse Config format: {error}\n")
        return false
    }

    if (!(root.value is _object)) {
        print("[SCENE ERROR] Config root is not an object.\n")
        return false
    }

    assume root_obj = root.value as _object
    if (!root_obj |> key_exists("entities")) {
        print("[SCENE ERROR] Config root missing 'entities' field.\n")
        return false
    }

    // Считываем списки ресурсов, сохраняем их в active_scene и запрашиваем их загрузку
    active_scene.requireMeshs <- parse_uint64_array(root_obj?["requireMeshs"] ?? null)
    active_scene.requireMaterials <- parse_uint64_array(root_obj?["requireMaterials"] ?? null)

    for (mesh_id in active_scene.requireMeshs) {
        RequestLoadMesh(mesh_id)
    }
    for (mat_id in active_scene.requireMaterials) {
        RequestLoadMaterial(mat_id)
    }

    let entities_cf = root_obj?["entities"] ?? null
    if (entities_cf == null || !(entities_cf.value is _array)) {
        print("[SCENE ERROR] 'entities' field is not an array.\n")
        return false
    }

    assume entities_arr = entities_cf.value as _array
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

        assume ent_obj = ent_cf.value as _object

        var entity_tag = "Entity_{ent_idx}"
        if (ent_obj |> key_exists("tag")) {
            let tag_cf = ent_obj?["tag"] ?? null
            if (tag_cf != null && tag_cf.value is _string) {
                entity_tag = from_CF(tag_cf, type<string>)
            }
        }

        unsafe {
            create_entity() @(eid : EntityId; var cmp : ComponentMap) {
                print("create entity name = {entity_tag}\n")
                assume cur_ent_obj = ent_cf.value as _object
                for (field_name in keys(cur_ent_obj)) {
                    let cf = cur_ent_obj?[field_name] ?? null
                    // print("field name = {field_name}\n")
                    parse_and_apply_field(cmp, entity_tag, field_name, cf, renames_captured, warnings_count, cf.typeName)
                }
            }
        }
        total_loaded++
    }

    commit()

    print("--------------------------------------------------\n")
    print(" [SCENE LOAD COMPLETED]\n")
    print(" Total Entities Loaded: {total_loaded}\n")
    print(" Meshes Requested: {length(active_scene.requireMeshs)}\n")
    print(" Materials Requested: {length(active_scene.requireMaterials)}\n")
    print(" Warnings / Skipped fields: {warnings_count}\n")
    print("==================================================\n\n")

    return true
}

def load_scene_from_config(config_text : string) : bool {
    var renames : table<string; string>
    return load_scene_from_config(config_text, renames)
}

struct SceneHeader {
    name : string
    requireMeshs : array<uint64>
    requireMaterials : array<uint64>
}

[async]
def GetSceneHeader(let path : string -const) : SceneHeader {
    let file_size : uint64 = await(FileReader::GetFileSize(path))
    if (file_size == 0ul)
        return false

    let bytes : array<uint8> <- await(FileReader::ReadFile(path, 0ul, uint(file_size)))

    var error : string
    let root = read_config(bytes, error)
    if (length(error) > 0 || root == null)
        return false

    if (!(root.value is _object))
        return false
    let root_obj & = root.value as _object

    var header = SceneHeader()
    header.name = (path |> last_index_of("/")) >= 0 ? slice(path, (path |> last_index_of("/")) + 1) : path
    header.requireMeshs <- parse_uint64_array(root_obj?["requireMeshs"] ?? null)
    header.requireMaterials <- parse_uint64_array(root_obj?["requireMaterials"] ?? null)

    yield <- header
}