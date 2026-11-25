options gen2
options relaxed_assign = false

module AssetPicker shared public

// Универсальный выбор ассета для полей инспектора и других редакторских панелей.
//
// Вид ассета задаёт аннотация самого поля:
//   @asset = "data:mat"
//   material : string
// kind — домен хранения, ext фильтрует всё дерево домена (не подпапку). Инспектор
// разбирает аннотацию из apply-схемы (`AssetFieldInfo`), поэтому отдельный реестр видов
// и привязка "имя компонента -> kind" не нужны.
//
// The picker does two things beyond a plain list:
//   * several domains at once (textures live in both engine_content/assets and game/assets);
//   * material/mesh compatibility (see tryeditor/MaterialCompat). This is the only place
//     that decides which .mat files to offer: the shader declares its mesh attributes and
//     the mesh reports the ones it has.
//
// The list comes from FileSystemUtils.FindFiles, i.e. the tree-revision cache; the picker
// itself keeps neither the file list nor the tree version.

require TryEditor
require tryeditor/FileSystemUtils
require tryeditor/MaterialCompat
require tryengine/AssetKind
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2

struct AssetPickerState {
    active           : bool
    owner_key        : string
    title            : string
    domains          : array<StorageDomain>
    ext_keys         : array<uint64>
    current_ref      : string
    selected_path    : string
    selected_guid    : uint64
    mesh_guid        : uint64
    filter_materials : bool
}

var private g_rows : table<string; ClickState>
var private g_filter : TextFilterState
var private g_popup : PopupState
var private g_state : AssetPickerState

def private OpenPicker(title : string; owner_key : string; current_ref : string;
                       domains : array<StorageDomain>; ext_keys : array<uint64>;
                       mesh_guid : uint64; filter_materials : bool) {
    g_state.active = true
    g_state.owner_key = owner_key
    g_state.title = title
    g_state.domains <- clone(domains)
    g_state.ext_keys <- clone(ext_keys)
    g_state.current_ref = current_ref
    g_state.selected_path = ""
    g_state.selected_guid = 0ul
    g_state.mesh_guid = mesh_guid
    g_state.filter_materials = filter_materials
    g_filter.value = ""
    clear(g_rows)
    g_popup.pending_open = true
}

// info — разобранная аннотация поля (@asset = "kind:ext").
// display_name — подпись поля, она же заголовок popup'а.
// owner_key — идентификатор поля, открывшего picker: popup рисуется только под ним,
// поэтому один и тот же state не конфликтует между полями.
// filter_materials — keep only .mat files compatible with mesh_guid.
def public OpenAssetPicker(info : AssetFieldInfo; display_name : string; current_ref : string; owner_key : string;
                           mesh_guid : uint64 = 0ul; filter_materials : bool = false) {
    if (!info.has || !empty(info.error)) {
        return
    }

    var domains : array<StorageDomain>
    domains |> push(AssetDomainOfKind(info.kind))
    var ext_keys : array<uint64>
    ext_keys |> push(asset_ext_key(info.ext))
    OpenPicker(display_name, owner_key, current_ref, domains, ext_keys, mesh_guid, filter_materials)
}

//! Textures live in two asset domains; the exact format does not matter, only that the
//! importer accepts .png/.jpg/.jpeg.
def public OpenTexturePicker(display_name : string; current_ref : string; owner_key : string) {
    var domains : array<StorageDomain>
    domains |> push(StorageDomain.Game)
    domains |> push(StorageDomain.Engine)

    var ext_keys : array<uint64>
    ext_keys |> push(asset_ext_key(".png"))
    ext_keys |> push(asset_ext_key(".jpg"))
    ext_keys |> push(asset_ext_key(".jpeg"))

    OpenPicker(display_name, owner_key, current_ref, domains, ext_keys, 0ul, false)
}

def public CloseAssetPicker() {
    if (!g_state.active) {
        return
    }
    g_state.active = false
    if (g_popup.open) {
        g_popup.pending_close = true
    }
}

def private AcceptFile(path : string; guid : uint64) : bool {
    if (!g_state.filter_materials) {
        return true
    }
    return MaterialCompat::IsCompatible(path, g_state.mesh_guid)
}

// Рисует popup, если его открыло поле owner_key. true — пользователь подтвердил выбор;
// сам выбор читается через AssetPickerSelection (пустая строка — очистить поле).
def public DrawAssetPickerPopup(owner_key : string) : bool {
    if (!g_state.active || g_state.owner_key != owner_key) {
        return false
    }

    var picked = false

    popup(g_popup, (text = "Select {g_state.title}", flags = ImGuiWindowFlags.None)) {
        if (empty(g_state.current_ref)) {
            text("Current: none")
        } else {
            text("Current: {g_state.current_ref}")
        }

        text_filter(g_filter, "Search", 320f)
        separator()

        child(PICKER_LIST, (text = "asset_picker_list", size = float2(480.0f, 320.0f),
         child_flags = ImGuiChildFlags.Borders, window_flags = ImGuiWindowFlags.None)) {
            var shown = 0
            // This is UI: copying the list once per frame is cheaper than a block that
            // captures the domain/extension inside ForEachFile.
            for (domain in g_state.domains) {
                for (ext_key in g_state.ext_keys) {
                    for (r in FindFiles(domain, ext_key)) {
                        let file = TreeFile(domain, r)
                        if (!AcceptFile(file.path, file.guid)) {
                            continue
                        }
                        if (!passes_filter(g_filter, file.path)) {
                            continue
                        }
                        shown++
                        with_id("asset_pick_{int(domain)}_{r.folder}_{r.file}") {
                            if (selectable_label(g_rows[file.path], file.path, g_state.selected_path == file.path)) {
                                g_state.selected_path = file.path
                                g_state.selected_guid = file.guid
                                if (IsMouseDoubleClicked(ImGuiMouseButton.Left)) {
                                    picked = true
                                }
                            }
                        }
                    }
                }
            }
            if (shown == 0) {
                if (g_state.filter_materials) {
                    text("No compatible materials")
                } else {
                    text("No matches")
                }
            }
        }

        separator()
        if (empty(g_state.selected_path)) {
            text_disabled("Select a file")
        } elif (button(BT_ASSET_SELECT, (text = "Select"))) {
            picked = true
        }
        same_line()
        if (button(BT_ASSET_CLEAR, (text = "Clear"))) {
            g_state.selected_path = ""
            g_state.selected_guid = 0ul
            picked = true
        }
        same_line()
        if (button(BT_ASSET_CANCEL, (text = "Cancel"))) {
            g_state.active = false
            g_popup.pending_close = true
        }
    }

    if (picked) {
        g_state.active = false
        g_popup.pending_close = true
    }
    return picked
}

// Выбранный путь после DrawAssetPickerPopup вернул true.
def public AssetPickerSelection() : string => g_state.selected_path

// guid of the selected file: needed for imported-asset fields (a texture in a .mat is
// addressed by guid, not path: the pak has no path table).
def public AssetPickerSelectionGuid() : uint64 => g_state.selected_guid
