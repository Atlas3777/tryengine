options gen2

module AssetBrowser shared private

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2
require imgui/imgui_style_builtin
require tryengine/Types
require tryeditor
require strings
require math

// Настройки темы Asset Browser
let AB_WINDOW_PADDING = float2(4.0f, 4.0f)
let AB_FRAME_PADDING  = float2(4.0f, 2.0f)
let AB_ITEM_SPACING   = float2(4.0f, 2.0f)
let AB_INDENT         = 10.0f

// Универсальная стилизация элементов ресурсов (папки и файлы)
def AssetStyle(selected, is_dir : bool; blk : block) {
    let bg = is_dir ? ImVec4(0.18f, 0.32f, 0.52f, 1.0f) : (selected ? ImVec4(0.20f, 0.48f, 0.32f, 1.0f) : ImVec4(0.22f, 0.24f, 0.28f, 1.0f))
    let hover = is_dir ? ImVec4(0.28f, 0.50f, 0.78f, 1.0f) : (selected ? ImVec4(0.28f, 0.58f, 0.40f, 1.0f) : ImVec4(0.30f, 0.34f, 0.38f, 1.0f))
    let align = is_list_view ? float2(0.0f, 0.5f) : float2(0.5f, 0.5f)

    with_style(
        (ImGuiCol.Button, bg),
        (ImGuiCol.ButtonHovered, hover),
        (ImGuiCol.Text, ImVec4(0.95f, 0.95f, 1.0f, 1.0f)),
        (ImGuiStyleVar.ButtonTextAlign, align),
        (ImGuiStyleVar.FrameRounding, is_dir ? 4.0f : 3.0f)
    ) {
        blk()
    }
}

// Состояние UI
var FOLDER_TREE_NODE : TreeNodeState
var W_SEARCH_FILTER : TextFilterState

var current_folder_idx : int = 0
var selected_file_folder_idx : int = -1
var selected_file_idx : int = -1

// Настройки отображения
var is_list_view : bool = true
var tile_icon_size : float = 70.0f
let MIN_TILE_SIZE : float = 35.0f
let MAX_TILE_SIZE : float = 200.0f

struct public AssetItem {
    guid : uint64
    index : uint
    name : string
}

var public PAYLOAD_VALUE : AssetItem

def public DrawAssetBrowser() {
    with_style(
        (ImGuiStyleVar.WindowPadding, AB_WINDOW_PADDING),
        (ImGuiStyleVar.FramePadding, AB_FRAME_PADDING),
        (ImGuiStyleVar.ItemSpacing, AB_ITEM_SPACING),
        (ImGuiStyleVar.IndentSpacing, AB_INDENT)) {
        window(ASSET_BROWSER_WIN, (text = "Asset Browser", closable = false, flags = ImGuiWindowFlags.None)) {
            checkbox(FOLDER_IDENT, (text = "EngineContent"))

            assume folders = FOLDER_IDENT.value ? get_engine_folders() : get_game_folders()
            if (length(folders) == 0) {
                text("No asset folders loaded.")
                return
            }

            // Горячие клавиши (Ctrl + L)
            let io & = unsafe(GetIO())
            if (IsWindowFocused(ImGuiFocusedFlags.RootAndChildWindows) && io.KeyCtrl && IsKeyPressed(ImGuiKey.L)) {
                is_list_view = !is_list_view
            }

            // Верхняя панель
            text("Search:")
            same_line()
            text_filter(W_SEARCH_FILTER)

            same_line()
            let view_label = is_list_view ? "View: List (Ctrl+L)" : "View: Grid (Ctrl+L)"
            if (button(BT_VIEW_MODE, (text = view_label))) {
                is_list_view = !is_list_view
            }

            separator(WI_SEP_0)

            // Основная область
            with_style((ImGuiStyleVar.ItemSpacing, float2(2.0f, 2.0f))) {
                columns_open(2, "asset_browser_columns", true)

                // Дерево папок (слева)
                child(FolderTreeRegion, (text = "FolderTreeRegion", size = float2(0.0f, 0.0f), child_flags = ImGuiChildFlags.Borders, window_flags = ImGuiWindowFlags.None)) {
                    with_style(
                        (ImGuiStyleVar.IndentSpacing, 8.0f),
                        (ImGuiStyleVar.FramePadding, float2(4.0f, 1.0f)),
                        (ImGuiStyleVar.ItemSpacing, float2(2.0f, 1.0f))
                    ) {
                        DrawFolderTreeNode(0, folders)
                    }
                }

                next_col()

                // Содержимое выбранной папки (справа)
                child(FolderContentRegion, (text = "FolderContentRegion", size = float2(0.0f, 0.0f), child_flags = ImGuiChildFlags.Borders, window_flags = ImGuiWindowFlags.None)) {
                    DrawFolderContent(current_folder_idx, folders)
                }

                columns_close()
            }
        }
    }
}

// ---------------------------------------------------------------------
// Дерево папок (левая панель)
// ---------------------------------------------------------------------
def DrawFolderTreeNode(folder_idx : int; let folders) {
    if (folder_idx < 0 || folder_idx >= length(folders)) {
        return
    }

    let folder_name = folders[folder_idx].name
    let sub_count = length(folders[folder_idx].subfolders)
    let has_children = sub_count > 0
    let is_selected = (folder_idx == current_folder_idx)

    var flags = ImGuiTreeNodeFlags.OpenOnArrow | ImGuiTreeNodeFlags.SpanAvailWidth
    if (!has_children) {
        flags |= ImGuiTreeNodeFlags.Leaf
    }
    if (is_selected) {
        flags |= ImGuiTreeNodeFlags.Selected
    }

    with_id("folder_tree_{folder_idx}") {
        let opened = tree_node_ex(FOLDER_TREE_NODE, (text = folder_name, flags = flags))
        if (IsItemClicked()) {
            current_folder_idx = folder_idx
        }

        if (opened) {
            if (has_children) {
                for (sub_idx in folders[folder_idx].subfolders) {
                    DrawFolderTreeNode(int(sub_idx), folders)
                }
            }
            tree_pop()
        }
    }
}

// ---------------------------------------------------------------------
// Содержимое директории (правая панель)
// ---------------------------------------------------------------------
def DrawFolderContent(folder_idx : int; let folders) {
    if (folder_idx < 0 || folder_idx >= length(folders)) {
        text("Select a directory")
        return
    }

    let io & = unsafe(GetIO())

    // Zoom в режиме сетки (Ctrl + Колесо мыши)
    if (!is_list_view && IsWindowHovered() && io.KeyCtrl && io.MouseWheel != 0.0f) {
        tile_icon_size = clamp(tile_icon_size + io.MouseWheel * 6.0f, MIN_TILE_SIZE, MAX_TILE_SIZE)
    }

    // Сброс выделения при клике по пустому месту
    if (IsMouseClicked(ImGuiMouseButton.Left) && IsWindowHovered() && !IsAnyItemHovered()) {
        selected_file_folder_idx = -1
        selected_file_idx = -1
    }

    text("Directory: {folders[folder_idx].name}")
    separator(WI_SEP_1)

    let sub_count = length(folders[folder_idx].subfolders)
    let file_count = length(folders[folder_idx].files)

    if (file_count == 0 && sub_count == 0) {
        text("Folder is empty")
        return
    }

    let avail_w = GetContentRegionAvail().x
    let items_per_row = max(1, int(avail_w / (tile_icon_size + 8.0f)))
    let item_size = is_list_view ? float2(avail_w, 22.0f) : float2(tile_icon_size, tile_icon_size)
    var visible_count = 0

    // 1. Подпапки
    for (i in range(sub_count)) {
        let child_idx = int(folders[folder_idx].subfolders[i])
        let child_name = folders[child_idx].name
        if (!passes_filter(W_SEARCH_FILTER, child_name)) {
            continue
        }

        if (!is_list_view && visible_count > 0 && (visible_count % items_per_row != 0)) {
            same_line()
        }

        with_id("dir_{child_idx}") {
            group(DIR_GRP) {
                AssetStyle(false, true) {
                    let btn_text = is_list_view ? "  [DIR] {child_name}" : "DIR"
                    if (button(BT_GO, (text = btn_text, size = item_size))) {
                        current_folder_idx = child_idx
                    }
                }
                if (!is_list_view) {
                    text("{child_name}")
                }
            }
        }
        visible_count++
    }

    // 2. Файлы
    for (i in range(file_count)) {
        assume file = folders[folder_idx].files[i]
        if (!passes_filter(W_SEARCH_FILTER, file.name)) {
            continue
        }

        if (!is_list_view && visible_count > 0 && (visible_count % items_per_row != 0)) {
            same_line()
        }

        let is_selected = (selected_file_folder_idx == folder_idx && selected_file_idx == i)

        with_id("file_{folder_idx}_{i}") {
            group(FILE_GRP) {
                AssetStyle(is_selected, false) {
                    let btn_text = is_list_view ? "  [FILE] {file.name}" : "FILE"
                    if (button(BT_GO, (text = btn_text, size = item_size))) {
                        selected_file_folder_idx = folder_idx
                        selected_file_idx = i
                    }

                    drag_drop_source(SOURCE_DD, (flags = ImGuiDragDropFlags.None)) {
                        PAYLOAD_VALUE.index = file.asset_idx
                        PAYLOAD_VALUE.name = file.name
                        PAYLOAD_VALUE.guid = file.guid
                        unsafe {
                            SetDragDropPayload("Asset", addr(PAYLOAD_VALUE),
                                uint64(typeinfo sizeof(PAYLOAD_VALUE)),
                                ImGuiCond.Once)
                        }
                        text("Dragging asset: {PAYLOAD_VALUE.name}\nGUID: {PAYLOAD_VALUE.guid}\nIndex: {PAYLOAD_VALUE.index}")
                    }
                }
                if (!is_list_view) {
                    text("{file.name}")
                }
            }
        }
        visible_count++
    }
}