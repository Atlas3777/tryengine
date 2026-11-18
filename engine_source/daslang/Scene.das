options gen2
options relaxed_assign = false

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

// Scene lifecycle event. The editor subscribes to spawn its camera after every load;
// the game has no listener and therefore no editor camera. `empty` marks the initial
// untitled scene that has no file yet.
typedef SceneLoadedListener = function<(name : string; path : string; empty : bool) : void>

var private g_scene_loaded_listeners : array<SceneLoadedListener>

def public RegisterSceneLoadedListener(listener : SceneLoadedListener) {
    g_scene_loaded_listeners |> push(listener)
}

def private NotifySceneLoaded(name, path : string; empty : bool) {
    for (listener in g_scene_loaded_listeners) {
        invoke(listener, name, path, empty)
    }
}

//! The editor's first load: there is no file yet, but it is still a scene load.
def public LoadEmptyScene() {
    UnloadActiveScene()
    scene_context.scene_name = ""
    scene_context.scene_path = ""
    NotifySceneLoaded("", "", true)
}

[async]
def LoadScene(name, path : string) : void {
    UnloadActiveScene()

    scene_context.scene_name = name
    scene_context.scene_path = path

    await(LoadSceneAsync(path))
    NotifySceneLoaded(name, path, false)
}

def UnloadActiveScene(){
    for (mat in active_scene.requireMaterials){
        ReleaseMaterialResource(mat)
    }
    for (mesh in active_scene.requireMeshs){
        ReleaseMeshResource(mesh)
    }
    active_scene <- Scene()
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

    scene_context.scene_name = name
    scene_context.scene_path = path
    NotifySceneLoaded(name, path, true)
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

//! Creates the editor camera when the current scene has none. Called by the editor at
//! startup and after play mode restores the pre-play DECS snapshot: without it the
//! viewport has no camera until a scene with one is loaded.
def public EnsureEditorCamera() {
    var cameras : array<EntityId>
    query() $(eid : EntityId; let editorCameraTag : bool) {
        cameras |> push(eid)
    }

    if (length(cameras) == 0) {
        CreateCamera()
        commit()
    } elif (length(cameras) > 1) {
        // One editor camera is an invariant: a loaded scene may contain a stale one,
        // and GameStart no longer creates a second.
        for (i in range(1, length(cameras))) {
            decs::delete_entity(cameras[i])
        }
        commit()
    }
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
        cmp.editorCameraTag := true
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

// requireMaterials: новый формат — строки-ссылки ("путь" или "guid:<id>"), старый — u64.
// Оба приводятся к id, чтобы загрузчик сцены и билд не знали про формат ссылки.
def private parse_asset_ref_array(cf : StructField?) : array<uint64> {
    var res : array<uint64>
    if (cf == null || !(cf.value is _array))
        return <- res

    assume arr = cf.value as _array
    for (elem in arr) {
        if (elem == null)
            continue

        var id : uint64 = 0ul
        if (elem.value is _string) {
            id = ResolveAssetRef(elem.value as _string)
        } elif (elem.value is _uint64) {
            id = elem.value as _uint64
        } elif (elem.value is _int64) {
            id = uint64(elem.value as _int64)
        } elif (elem.value is _uint) {
            id = uint64(elem.value as _uint)
        } elif (elem.value is _int) {
            id = uint64(elem.value as _int)
        }

        if (id != 0ul && !res.contains(id))
            res |> push(id)
    }
    return <- res
}

struct private SaveSceneDTO {
    requireMeshs : array<uint64>
    requireMaterials : array<string>
    entities : array<StructField?>
}

def private save_scene_to_config() : string {
    var entities_list : array<StructField?>

    // 1. Собираем сущности из DECS
    for (arch in decsState.allArchetypes) {
        for (i in range64(arch.size)) {
            var entity_obj : table<string; StructField?>

            for (comp in arch.components) {
                // Указатели (mesh/material) — runtime-ссылки, в сцену они не пишутся.
                if (comp.info.basicType == Type.tPointer)
                    continue
                // Компоненты с "_" — runtime-only (маркеры загрузки ресурсов и т.п.).
                if (starts_with(comp.name, "_"))
                    continue

                var cf_val = ComponentToCF(comp, i)
                if (cf_val == null) {
                    print("[SCENE WARNING] Component '{comp.name}' of type '{comp.info.fullName}' is not registered for saving; skipped. Mark the struct [DecsSerializable].\n")
                    continue
                }
                cf_val.name = comp.name
                entity_obj[comp.name] = cf_val
            }

            // The editor camera is editor state, not scene content.
            if (entity_obj |> key_exists("editorCameraTag")) {
                continue
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

    // В сцену пишем авторские ссылки, а не id: у авторитетного материала это путь,
    // у импортированного — guid. active_scene держит уже разрешённые id для выгрузки.
    var material_refs : array<string>
    query() $(materialRef : MaterialRef) {
        if (!empty(materialRef.path) && !material_refs.contains(materialRef.path))
            material_refs |> push(materialRef.path)
        let material_id = ResolveAssetRef(materialRef.path)
        if (material_id != 0ul && !active_scene.requireMaterials.contains(material_id))
            active_scene.requireMaterials |> push(material_id)
    }

    // 3. Формируем временный DTO для рефлексии CF()
    var dto <- SaveSceneDTO(
        requireMeshs := active_scene.requireMeshs,
        requireMaterials <- material_refs,
        entities <- entities_list
    )

    return write_config(CF(dto))
}


// Значение компонента пишется apply-based writer'ом, зарегистрированным для его типа
// (CF), поэтому аннотации полей обрабатываются так же, как при обычной сериализации.
def private ComponentToCF(comp : Component&; index : int64) : StructField? {
    let offset = index * int64(comp.stride)
    unsafe {
        let ptr = reinterpret<void?>(addr(comp.data[offset]))
        return DecsWriteComponent(ptr, comp.info.fullName, comp.name)
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

    // Старый формат: materialGuid: u64. Превращаем id в каноническую ссылку, чтобы
    // сцена не зависела от того, чем этот id был раньше.
    if (field_name == "materialGuid") {
        if (cf.value is _uint64) {
            let ref = AssetRefOf(cf.value as _uint64)
            if (!empty(ref))
                cmp |> set("materialRef", MaterialRef(path = ref))
        }
        return true
    }

    // Старый формат materialRef: string. Новый — обёртка MaterialRef(path): обновляем
    // старые сцены на лету, чтобы Render.das видел один и тот же тип компонента.
    if (field_name == "materialRef" && cf.value is _string) {
        cmp |> set("materialRef", MaterialRef(path = cf.value as _string))
        return true
    }

    if (ApplyDecsConfigField(cmp, target_name, cf, type_name)) {
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
    active_scene.requireMaterials <- parse_asset_ref_array(root_obj?["requireMaterials"] ?? null)

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
                    if (starts_with(field_name, "_"))
                        continue
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

    var header <- SceneHeader()
    header.name = (path |> last_index_of("/")) >= 0 ? slice(path, (path |> last_index_of("/")) + 1) : path
    header.requireMeshs <- parse_uint64_array(root_obj?["requireMeshs"] ?? null)
    header.requireMaterials <- parse_asset_ref_array(root_obj?["requireMaterials"] ?? null)

    yield <- header
}