options gen2
options relaxed_assign = false

module AssetPicker shared public

// Универсальный выбор ассета для полей инспектора и других редакторских панелей.
//
// Тип ассета задаёт сам тип значения: поле — это структура-обёртка с аннотацией
//   [asset(kind = "data", title = "Material", ext = ".mat")]
//   struct MaterialRef { path : string }
// kind — домен хранения, ext фильтрует всё дерево домена (не подпапку).
// Инспектор находит такую обёртку по имени типа поля, поэтому отдельный реестр видов
// и привязка "имя компонента -> kind" больше не нужны.
//
// Список строится через FileSystemUtils.ForEachFile, то есть по кешу ревизии дерева;
// сам picker не хранит ни список файлов, ни версию дерева.

require TryEditor
require tryeditor/FileSystemUtils
require tryengine/AssetKind
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2

struct AssetPickerState {
    active        : bool
    owner_key     : string
    title         : string
    domain        : StorageDomain
    ext_key       : uint64
    current_ref   : string
    selected_path : string
}

var private g_rows : table<string; ClickState>
var private g_filter : TextFilterState
var private g_popup : PopupState
var private g_state : AssetPickerState

// type_name — имя структуры-обёртки, а не зарегистрированный строковый kind.
// owner_key — идентификатор поля, открывшего picker: popup рисуется только под ним,
// поэтому один и тот же state не конфликтует между полями.
def public OpenAssetPicker(type_name : string; current_ref : string; owner_key : string) {
    var info : AssetTypeInfo
    if (!GetAssetTypeInfo(type_name, info)) {
        return
    }

    g_state.active = true
    g_state.owner_key = owner_key
    g_state.title = AssetTitleOf(info)
    g_state.domain = AssetDomainOfKind(info.kind)
    g_state.ext_key = asset_ext_key(info.ext)
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
            ForEachFile(g_state.domain, g_state.ext_key) $(r) {
                let file = TreeFile(g_state.domain, r)
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
def public AssetPickerSelection() : string => g_state.selected_path
