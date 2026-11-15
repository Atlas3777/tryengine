options gen2
options relaxed_assign = false

module AssetBrowser shared public

require TryEditor

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2
require imgui/imgui_style_builtin
require tryeditor/FileSystemUtils
require strings
require math

// Настройки темы Asset Browser
let AB_WINDOW_PADDING = float2(4.0f, 4.0f)
let AB_FRAME_PADDING = float2(4.0f, 2.0f)
let AB_ITEM_SPACING = float2(4.0f, 2.0f)
let AB_INDENT = 10.0f

// Универсальная стилизация элементов ресурсов (папки и файлы)
def AssetStyle(selected : bool; is_dir : bool; blk : block) {
    let bg = selected ? ImVec4(0.20f, 0.48f, 0.32f, 1.0f) : (is_dir ? ImVec4(0.18f, 0.32f, 0.52f, 1.0f) : ImVec4(0.22f, 0.24f, 0.28f, 1.0f))
    let hover = selected ? ImVec4(0.28f, 0.58f, 0.40f, 1.0f) : (is_dir ? ImVec4(0.28f, 0.50f, 0.78f, 1.0f) : ImVec4(0.30f, 0.34f, 0.38f, 1.0f))
    let align = float2(0.0f, 0.5f)

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

// Корень, который сейчас показан. Домен — это корень: game/assets и engine_content/assets
// содержат ассеты (.meta + guid), game/content и game/data — авторитетные файлы.
var current_domain : StorageDomain = StorageDomain.Game
var current_folder_idx : int = 0
var selected_file_folder_idx : int = -1
var selected_file_idx : int = -1

// Payload перетаскивания. Поля и порядок совпадают с AssetItem: на принимающей стороне
// (SceneViewPort) буфер читается как AssetItem, поэтому layout менять нельзя.
struct AssetDrag {
    guid : uint64
    ext : uint64
    name : string
    path : string
}

var public PAYLOAD_VALUE : AssetDrag

def public DrawAssetBrowser() {
    with_style(
        (ImGuiStyleVar.WindowPadding, AB_WINDOW_PADDING),
        (ImGuiStyleVar.FramePadding, AB_FRAME_PADDING),
        (ImGuiStyleVar.ItemSpacing, AB_ITEM_SPACING),
        (ImGuiStyleVar.IndentSpacing, AB_INDENT)) {
        window(ASSET_BROWSER_WIN, (text = "Asset Browser", closable = false, flags = ImGuiWindowFlags.None)) {
            DrawRootSelector()

            assume folders = get_folders(current_domain)
            if (length(folders) == 0) {
                text("No folders under this root.")
                return
            }
            if (current_folder_idx >= length(folders)) {
                current_folder_idx = 0
            }

            text_filter(W_SEARCH_FILTER, "Search", 200f)

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
                    DrawFolderContent(folders, current_folder_idx)
                }

                columns_close()
            }
        }
    }
}

// ---------------------------------------------------------------------
// Выбор корня и пересборка деревьев
// ---------------------------------------------------------------------
def private ResetSelection() {
    current_folder_idx = 0
    selected_file_folder_idx = -1
    selected_file_idx = -1
}

def private SelectDomain(domain : StorageDomain) {
    if (current_domain == domain) {
        return
    }
    current_domain = domain
    ResetSelection()
}

def private DrawDomainButton(domain : StorageDomain; text : string) {
    with_id("domain_{int(domain)}") {
        AssetStyle(current_domain == domain, false) {
            if (button(BT_DOMAIN, (text = text))) {
                SelectDomain(domain)
            }
        }
    }
}

def private DrawRootSelector() {
    DrawDomainButton(StorageDomain.Game, "Game")
    same_line()
    DrawDomainButton(StorageDomain.Engine, "Engine")
    same_line()
    DrawDomainButton(StorageDomain.Content, "Content")
    same_line()
    DrawDomainButton(StorageDomain.Data, "Data")
    same_line()

    // Полная пересборка деревьев: пока нет файлового вотчера, новые файлы видны только так.
    if (button(BT_REFRESH_TREE, (text = "Refresh"))) {
        RefreshAssetTrees()
        ResetSelection()
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
// Поиск по всему корню
// ---------------------------------------------------------------------
def DrawSearchResults() {
    // Индексы берём из кеша запросов: обход дерева повторяется только при смене ревизии.
    let refs <- FindFiles(current_domain, EXT_ANY)
    assume folders = get_folders(current_domain)

    text("Search results across root:")
    separator(WI_SEP_1)

    let avail_w = GetContentRegionAvail().x
    let item_size = float2(avail_w, 22.0f)
    var visible_count = 0

    for (r in refs) {
        assume folder = folders[r.folder]
        assume file = folder.files[r.file]
        if (!passes_filter(W_SEARCH_FILTER, file.name)) {
            continue
        }

        let is_selected = (selected_file_folder_idx == r.folder && selected_file_idx == r.file)

        with_id("file_{r.folder}_{r.file}") {
            group(FILE_GRP) {
                AssetStyle(is_selected, false) {
                    let btn_text = "  [FILE] {file.name}  ({folder.name})"
                    if (button(BT_GO, (text = btn_text, size = item_size))) {
                        selected_file_folder_idx = r.folder
                        selected_file_idx = r.file
                    }

                    drag_drop_source(SOURCE_DD, (flags = ImGuiDragDropFlags.None)) {
                        // Строки дерева принадлежат пайплайну; в payload копируем свои.
                        PAYLOAD_VALUE.name = "{file.name}"
                        PAYLOAD_VALUE.path = "{file.path}"
                        PAYLOAD_VALUE.guid = file.guid
                        PAYLOAD_VALUE.ext = file.ext
                        unsafe {
                            SetDragDropPayload("Asset", addr(PAYLOAD_VALUE),
                                uint64(typeinfo sizeof(PAYLOAD_VALUE)),
                                ImGuiCond.Once)
                        }
                        text("Dragging asset: {PAYLOAD_VALUE.name}\nGUID: {PAYLOAD_VALUE.guid}\nPath: {PAYLOAD_VALUE.path}")
                    }
                }
            }
        }
        visible_count++
    }

    if (visible_count == 0) {
        text("No matching files found.")
    }
}

// ---------------------------------------------------------------------
// Содержимое директории (правая панель)
// ---------------------------------------------------------------------
def DrawFolderContent(let folders; folder_idx : int) {
    if (folder_idx < 0 || folder_idx >= length(folders)) {
        text("Select a directory")
        return
    }

    // Сброс выделения при клике по пустому месту
    if (IsMouseClicked(ImGuiMouseButton.Left) && IsWindowHovered() && !IsAnyItemHovered()) {
        selected_file_folder_idx = -1
        selected_file_idx = -1
    }

    // --- РЕЖИМ ГЛОБАЛЬНОГО ПОИСКА ПО ТЕКУЩЕМУ КОРНЮ ---
    if (is_active(W_SEARCH_FILTER)) {
        DrawSearchResults()
        return
    }

    // --- ОБЫЧНЫЙ РЕЖИМ (ОТОБРАЖЕНИЕ ТОЛЬКО ТЕКУЩЕЙ ПАПКИ) ---
    text("Directory: {folders[folder_idx].path}")
    separator(WI_SEP_1)

    let sub_count = length(folders[folder_idx].subfolders)
    let file_count = length(folders[folder_idx].files)

    if (file_count == 0 && sub_count == 0) {
        text("Folder is empty")
        return
    }

    let avail_w = GetContentRegionAvail().x
    let item_size = float2(avail_w, 22.0f)

    // 1. Подпапки текущей директории
    for (i in range(sub_count)) {
        let child_idx = int(folders[folder_idx].subfolders[i])
        let child_name = folders[child_idx].name
        let is_selected = (selected_file_folder_idx == child_idx && selected_file_idx == -1)

        with_id("dir_{child_idx}") {
            group(DIR_GRP) {
                AssetStyle(is_selected, true) {
                    let btn_text = "  [DIR] {child_name}"

                    // Однократный клик выделяет папку
                    if (button(BT_GO, (text = btn_text, size = item_size))) {
                        selected_file_folder_idx = child_idx
                        selected_file_idx = -1
                    }

                    // Двойной клик переходит внутрь папки
                    if (IsItemHovered() && IsMouseDoubleClicked(ImGuiMouseButton.Left)) {
                        current_folder_idx = child_idx
                    }
                }
            }
        }
    }

    // 2. Файлы текущей директории
    for (i in range(file_count)) {
        assume file = folders[folder_idx].files[i]

        let is_selected = (selected_file_folder_idx == folder_idx && selected_file_idx == i)

        with_id("file_{folder_idx}_{i}") {
            group(FILE_GRP) {
                AssetStyle(is_selected, false) {
                    let btn_text = "  [FILE] {file.name}"
                    if (button(BT_GO, (text = btn_text, size = item_size))) {
                        selected_file_folder_idx = folder_idx
                        selected_file_idx = i
                    }

                    drag_drop_source(SOURCE_DD, (flags = ImGuiDragDropFlags.None)) {
                        // Строки дерева принадлежат пайплайну; в payload копируем свои.
                        PAYLOAD_VALUE.name = "{file.name}"
                        PAYLOAD_VALUE.path = "{file.path}"
                        PAYLOAD_VALUE.guid = file.guid
                        PAYLOAD_VALUE.ext = file.ext
                        unsafe {
                            SetDragDropPayload("Asset", addr(PAYLOAD_VALUE),
                                uint64(typeinfo sizeof(PAYLOAD_VALUE)),
                                ImGuiCond.Once)
                        }
                        text("Dragging asset: {PAYLOAD_VALUE.name}\nGUID: {PAYLOAD_VALUE.guid}\nPath: {PAYLOAD_VALUE.path}")
                    }
                }
            }
        }
    }
}
