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

// Компактные настройки темы Asset Browser
let AB_WINDOW_PADDING = float2(4.0f, 4.0f)
let AB_FRAME_PADDING  = float2(4.0f, 2.0f)
let AB_ITEM_SPACING   = float2(4.0f, 2.0f)
let AB_INDENT         = 10.0f

// Folder List
def FolderListIconStyle(blk : block) {
    with_style(
        (ImGuiCol.Button, ImVec4(0.18f, 0.32f, 0.52f, 1.0f)),
        (ImGuiCol.ButtonHovered, ImVec4(0.28f, 0.50f, 0.78f, 1.0f)),
        (ImGuiStyleVar.FrameRounding, 3.0f)
    ) {
        blk()
    }
}

// Folder Text
def FolderListTextStyle(blk : block) {
    with_style(
        (ImGuiCol.Button, ImVec4(0.12f, 0.14f, 0.18f, 0.40f)),
        (ImGuiCol.ButtonHovered, ImVec4(0.22f, 0.35f, 0.55f, 0.60f)),
        (ImGuiCol.Text, ImVec4(0.95f, 0.95f, 1.0f, 1.0f)),
        (ImGuiStyleVar.ButtonTextAlign, float2(0.0f, 0.5f))
    ) {
        blk()
    }
}

// Folder Grid
def FolderGridStyle(blk : block) {
    with_style(
        (ImGuiCol.Button, ImVec4(0.18f, 0.32f, 0.52f, 1.0f)),
        (ImGuiCol.ButtonHovered, ImVec4(0.28f, 0.50f, 0.78f, 1.0f)),
        (ImGuiCol.ButtonActive, ImVec4(0.14f, 0.24f, 0.40f, 1.0f)),
        (ImGuiCol.Text, ImVec4(0.95f, 0.95f, 1.0f, 1.0f)),
        (ImGuiStyleVar.FrameRounding, 6.0f)
    ) {
        blk()
    }
}

// File Icon
def FileIconStyle(selected : bool; blk : block) {
    let bg = selected ? ImVec4(0.20f, 0.48f, 0.32f, 1.0f) : ImVec4(0.22f, 0.24f, 0.28f, 1.0f)

    with_style(
        (ImGuiCol.Button, bg),
        (ImGuiStyleVar.FrameRounding, 3.0f)
    ) {
        blk()
    }
}

// File Text
def FileTextStyle(selected : bool; blk : block) {
    let bg = selected ? ImVec4(0.20f, 0.48f, 0.32f, 0.35f) : ImVec4(0.12f, 0.13f, 0.15f, 0.30f)
    let hover = selected ? ImVec4(0.25f, 0.55f, 0.38f, 0.50f) : ImVec4(0.25f, 0.28f, 0.32f, 0.50f)

    with_style(
        (ImGuiCol.Button, bg),
        (ImGuiCol.ButtonHovered, hover),
        (ImGuiCol.Text, ImVec4(0.92f, 0.92f, 0.95f, 1.0f)),
        (ImGuiStyleVar.ButtonTextAlign, float2(0.0f, 0.5f))
    ) {
        blk()
    }
}

// File Grid
def FileGridStyle(selected : bool; rounding : float; blk : block) {
    let bg = selected ? ImVec4(0.20f, 0.48f, 0.32f, 1.0f) : ImVec4(0.22f, 0.24f, 0.28f, 1.0f)
    let hover = selected ? ImVec4(0.28f, 0.58f, 0.40f, 1.0f) : ImVec4(0.30f, 0.34f, 0.38f, 1.0f)
    let active = selected ? ImVec4(0.15f, 0.38f, 0.24f, 1.0f) : ImVec4(0.18f, 0.20f, 0.24f, 1.0f)

    with_style(
        (ImGuiCol.Button, bg),
        (ImGuiCol.ButtonHovered, hover),
        (ImGuiCol.ButtonActive, active),
        (ImGuiCol.Text, ImVec4(0.92f, 0.92f, 0.95f, 1.0f)),
        (ImGuiStyleVar.FrameRounding, rounding)
    ) {
        blk()
    }
}

// Состояние дерева папок
var FOLDER_TREE_NODE : TreeNodeState

// Текстовый фильтр поиска ресурсов
var W_SEARCH_FILTER : TextFilterState

// Состояние выбора
var current_folder_idx : int = 0
var selected_file_folder_idx : int = -1
var selected_file_idx : int = -1

// Настройки отображения и масштабирования
var is_list_view : bool = true
var tile_icon_size : float = 70.0f
let MIN_TILE_SIZE : float = 35.0f
let MAX_TILE_SIZE : float = 200.0f

struct public AssetItem{
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
            let total_folders = length(folders)


            if (total_folders == 0) {
                text("No asset folders loaded.")
                return
            }

            // Обработка горячих клавиш (Ctrl + L)
            let io & = unsafe(GetIO())
            if (IsWindowFocused(ImGuiFocusedFlags.RootAndChildWindows)) {
                if (io.KeyCtrl && IsKeyPressed(ImGuiKey.L)) {
                    is_list_view = !is_list_view
                }
            }

            // Панель поиска и переключатель режима вида
            text("Search:")
            same_line()
            text_filter(W_SEARCH_FILTER)

            same_line()
            let view_label = is_list_view ? "View: List (Ctrl+L)" : "View: Grid (Ctrl+L)"
            if (button(BT_VIEW_MODE, (text = view_label))) {
                is_list_view = !is_list_view
            }

            separator(WI_SEP_0)

            // Колонки с уменьшенным ItemSpacing
            with_style((ImGuiStyleVar.ItemSpacing, float2(2.0f, 2.0f))) {
                columns_open(2, "asset_browser_columns", true)

                // Левая колонка: Дерево папок
                child(FolderTreeRegion, (text = "FolderTreeRegion",
                                 size = float2(0.0f, 0.0f),
                                 child_flags = ImGuiChildFlags.Borders,
                                 window_flags = ImGuiWindowFlags.None)) {
                    with_style(
                        (ImGuiStyleVar.IndentSpacing, 8.0f),
                        (ImGuiStyleVar.FramePadding, float2(4.0f, 1.0f)),
                        (ImGuiStyleVar.ItemSpacing, float2(2.0f, 1.0f))
                    ) {
                        DrawFolderTreeNode(0, folders)
                    }
                }

                next_col()

                // Правая колонка: Содержимое выбранной папки
                child(FolderContentRegion, (text = "FolderContentRegion",
                                 size = float2(0.0f, 0.0f),
                                 child_flags = ImGuiChildFlags.Borders,
                                 window_flags = ImGuiWindowFlags.None)) {
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
// Содержимое текущей директории (правая панель)
// ---------------------------------------------------------------------
def DrawFolderContent(folder_idx : int; let folders) {
    if (folder_idx < 0 || folder_idx >= length(folders)) {
        text("Select a directory")
        return
    }

    let io & = unsafe(GetIO())

    // Zoom в режиме сетки (Ctrl + Колесо мыши)
    if (!is_list_view && IsWindowHovered() && io.KeyCtrl && io.MouseWheel != 0.0f) {
        let zoom_step = 6.0f
        tile_icon_size = clamp(tile_icon_size + io.MouseWheel * zoom_step, MIN_TILE_SIZE, MAX_TILE_SIZE)
    }

    // Сброс выделения при клике по пустому пространству
    if (IsMouseClicked(ImGuiMouseButton.Left) && IsWindowHovered() && !IsAnyItemHovered()) {
        selected_file_folder_idx = -1
        selected_file_idx = -1
    }

    let folder_name = folders[folder_idx].name
    text("Directory: {folder_name}")
    separator(WI_SEP_1)

    let sub_count = length(folders[folder_idx].subfolders)
    let file_count = length(folders[folder_idx].files)

    if (file_count == 0 && sub_count == 0) {
        text("Folder is empty")
        return
    }

    let avail_w = GetContentRegionAvail().x

    if (is_list_view) {
        let list_row_h = 24.0f
        let icon_w = 38.0f

        // 1. Подпапки (синий стиль)
        for (i in range(sub_count)) {
            let child_idx = int(folders[folder_idx].subfolders[i])
            let child_name = folders[child_idx].name

            if (!passes_filter(W_SEARCH_FILTER, child_name)) {
                continue
            }

            with_id("dir_row_{child_idx}") {
                group(DIR_LIST_GRP) {
                    FolderListIconStyle() {
                        if (button(BT_GO_VERT, (text = "DIR", size = float2(icon_w, list_row_h)))) {
                            current_folder_idx = child_idx
                        }
                    }

                    same_line()

                    let remaining_w = max(100.0f, avail_w - icon_w - 8.0f)
                    FolderListTextStyle() {
                        if (button(BT_GO2, (text = "  {child_name}", size = float2(remaining_w, list_row_h)))) {
                            current_folder_idx = child_idx
                        }
                    }
                }
            }
        }

        // 2. Файлы
        for (i in range(file_count)) {
            let file_name = folders[folder_idx].files[i].name

            if (!passes_filter(W_SEARCH_FILTER, file_name)) {
                continue
            }

            with_id("file_row_{folder_idx}_{i}") {
                let is_selected = (selected_file_folder_idx == folder_idx && selected_file_idx == i)

                group(FILE_LIST_GRP) {
                    FileIconStyle(is_selected) {
                        if (button(BT_GO, (text = "FILE", size = float2(icon_w, list_row_h)))) {
                            selected_file_folder_idx = folder_idx
                            selected_file_idx = i
                        }
                        drag_drop_source(SOURCE_DD, (flags = ImGuiDragDropFlags.None)) {
                            PAYLOAD_VALUE.index = folders[folder_idx].files[i].asset_idx
                            PAYLOAD_VALUE.name = folders[folder_idx].files[i].name
                            PAYLOAD_VALUE.guid = folders[folder_idx].files[i].guid
                            unsafe {
                                SetDragDropPayload("Asset", addr(PAYLOAD_VALUE),
                                   uint64(typeinfo sizeof(PAYLOAD_VALUE)),
                                   ImGuiCond.Once)
                                }
                            text("Dragging asset: {PAYLOAD_VALUE.name}")
                            text("GUID: {PAYLOAD_VALUE.guid}")
                            text("Dragging asset idx: {PAYLOAD_VALUE.index}")
                        }
                    }

                    same_line()

                    let remaining_w = max(100.0f, avail_w - icon_w - 8.0f)
                    FileTextStyle(is_selected) {
                        if (button(BT_GO_RIGHT, (text = "  {file_name}", size = float2(remaining_w, list_row_h)))) {
                            selected_file_folder_idx = folder_idx
                            selected_file_idx = i
                        }
                    }
                }
            }
        }
    } else {
        let tile_size = float2(tile_icon_size, tile_icon_size)
        let tile_rounding = 6.0f
        let items_per_row = max(1, int(avail_w / (tile_size.x + 12.0f)))
        var visible_item_idx = 0

        // 1. Подпапки (плитки)
        for (i in range(sub_count)) {
            let child_idx = int(folders[folder_idx].subfolders[i])
            let child_name = folders[child_idx].name

            if (!passes_filter(W_SEARCH_FILTER, child_name)) {
                continue
            }

            with_id("dir_grid_{child_idx}") {
                if (visible_item_idx > 0 && (visible_item_idx % items_per_row != 0)) {
                    same_line()
                }

                group(DIR_GRID_GRP) {
                    FolderGridStyle() {
                        if (button(BT_GO_GRIG, (text = "DIR", size = tile_size))) {
                            current_folder_idx = child_idx
                        }
                    }

                    text("{child_name}")
                }
            }
            visible_item_idx++
        }

        // 2. Файлы (плитки)
        for (i in range(file_count)) {
            let file_name = folders[folder_idx].files[i].name

            if (!passes_filter(W_SEARCH_FILTER, file_name)) {
                continue
            }

            with_id("file_grid_{folder_idx}_{i}") {
                if (visible_item_idx > 0 && (visible_item_idx % items_per_row != 0)) {
                    same_line()
                }

                let is_selected = (selected_file_folder_idx == folder_idx && selected_file_idx == i)

                group(FILE_GRID_GRP) {
                    FileGridStyle(is_selected, tile_rounding) {
                        if (button(BT_GO, (text = "FILE", size = tile_size))) {
                            selected_file_folder_idx = folder_idx
                            selected_file_idx = i
                        }
                    }

                    text("{file_name}")
                }
            }
            visible_item_idx++
        }
    }
}