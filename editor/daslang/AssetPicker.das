options gen2
options relaxed_assign = false

module AssetPicker shared public

// Универсальный выбор ассета для полей инспектора и других редакторских панелей.
//
// Тип ассета — это зарегистрированный "kind": домен дерева (game/data, game/content, ...)
// и ключ расширения. Список строится через FileSystemUtils.FindFiles/ForEachFile, поэтому
// обход дерева выполняется только при смене его ревизии, а не на каждый кадр.
//
// Инспектор открывает picker по аннотации поля @asset = "material" или (для компонентов
// без схемы, как materialRef) через RegisterComponentAssetPicker.

require TryEditor
require tryeditor/FileSystemUtils
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2
require strings

struct AssetPickerKind {
    title   : string
    domain  : StorageDomain
    ext_key : uint64
}

struct AssetPickerState {
    active        : bool
    owner_key     : string
    kind          : string
    current_ref   : string
    selected_path : string
}

var private g_kinds : table<string; AssetPickerKind>
var private g_rows : table<string; ClickState>
var private g_filter : TextFilterState
var private g_popup : PopupState
var private g_state : AssetPickerState

def public RegisterAssetPickerKind(name : string; title : string; domain : StorageDomain; ext_key : uint64) {
    g_kinds[name] = AssetPickerKind(title = title, domain = domain, ext_key = ext_key)
}

def public HasAssetPickerKind(name : string) : bool {
    return key_exists(g_kinds, name)
}

[init]
def private RegisterBuiltinAssetPickerKinds() {
    RegisterAssetPickerKind("material", "Material", StorageDomain.Data, EXT_MAT)
    RegisterAssetPickerKind("scene", "Scene", StorageDomain.Content, EXT_SCENE)
    RegisterAssetPickerKind("prefab", "Prefab", StorageDomain.Content, EXT_PREFAB)
}

// owner_key — идентификатор поля, открывшего picker: popup рисуется только под ним,
// поэтому один и тот же state не конфликтует между полями.
def public OpenAssetPicker(kind : string; current_ref : string; owner_key : string) {
    if (!key_exists(g_kinds, kind)) {
        return
    }
    g_state.active = true
    g_state.owner_key = owner_key
    g_state.kind = kind
    g_state.current_ref = current_ref
    g_state.selected_path = ""
    g_filter.value = ""
    clear(g_rows)
    g_popup.pending_open = true
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

// Рисует popup, если его открыло поле owner_key. true — пользователь подтвердил выбор;
// сам выбор читается через AssetPickerSelection (пустая строка — очистить поле).
def public DrawAssetPickerPopup(owner_key : string) : bool {
    if (!g_state.active || g_state.owner_key != owner_key) {
        return false
    }

    let kind = g_kinds[g_state.kind]
    var picked = false

    popup(g_popup, (text = "Select {kind.title}", flags = ImGuiWindowFlags.None)) {
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
            ForEachFile(kind.domain, kind.ext_key) $(r) {
                let file = TreeFile(kind.domain, r)
                if (!passes_filter(g_filter, file.path)) {
                    return
                }
                shown++
                with_id("asset_pick_{r.folder}_{r.file}") {
                    if (selectable_label(g_rows[file.path], file.path, g_state.selected_path == file.path)) {
                        g_state.selected_path = file.path
                        if (IsMouseDoubleClicked(ImGuiMouseButton.Left)) {
                            picked = true
                        }
                    }
                }
            }
            if (shown == 0) {
                text("No matches")
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
def public AssetPickerSelection() : string {
    return g_state.selected_path
}
