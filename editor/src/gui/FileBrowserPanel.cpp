#include "editor/gui/FileBrowserPanel.hpp"

#include "editor/EditorGUIUtils.hpp"
#include "editor/SceneManagerController.hpp"
#include "editor/asset_factories/AssetsFactoryManager.hpp"
#include "editor/import/ImportSystem.hpp"
#include "editor/meta/MetaSerializer.hpp"

namespace tryeditor {

FileBrowserPanel::FileBrowserPanel(ImportSystem& import_system, SelectionManager& selection_maanger,
                                   AssetsFactoryManager& factory_manager, tryengine::core::SceneManager& scene_manager)
    : import_system_(import_system), selection_manager_(selection_maanger), factory_manager_(factory_manager), scene_manager_(scene_manager) {
    // Инициализируем корни для игры и движка
    game_root_ = std::filesystem::current_path() / "game/assets/";
    engine_root_ = std::filesystem::current_path() / "engine_content/assets/";

    if (!std::filesystem::exists(game_root_)) {
        std::filesystem::create_directories(game_root_);
    }
    if (!std::filesystem::exists(engine_root_)) {
        std::filesystem::create_directories(engine_root_);
    }

    current_mode_ = ContentMode::Game;
    root_directory_ = game_root_;
    selected_directory_ = root_directory_;
}

void FileBrowserPanel::OnImGuiRender(entt::registry& reg) {
    ImGui::Begin("Project Tree");

    // --- Переключатель контента ---
    int mode = static_cast<int>(current_mode_);
    if (ImGui::RadioButton("Game Content", &mode, 0)) {
        current_mode_ = ContentMode::Game;
        root_directory_ = game_root_;
        selected_directory_ = root_directory_;
        // editor_context_.selected_asset_path.clear();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Engine Content", &mode, 1)) {
        current_mode_ = ContentMode::Engine;
        root_directory_ = engine_root_;
        selected_directory_ = root_directory_;
        // editor_context_.selected_asset_path.clear();
    }
    ImGui::Separator();
    // ------------------------------

    DrawDirectoryTree(root_directory_);
    ImGui::End();

    ImGui::Begin("Project Content");
    DrawDirectoryContent();
    ImGui::End();
}

void FileBrowserPanel::DrawDirectoryTree(const std::filesystem::path& directoryPath) {
    std::string filenameString = directoryPath.filename().string();
    if (filenameString.empty()) {
        filenameString = directoryPath.string();
    }

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (directoryPath == root_directory_) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }

    if (selected_directory_ == directoryPath) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool has_subdirs = false;
    for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
        if (entry.is_directory()) {
            has_subdirs = true;
            break;
        }
    }

    if (!has_subdirs) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    ImGui::PushID(directoryPath.string().c_str());

    const char* icon = has_subdirs ? "(+) " : "( ) ";
    std::string label = icon + filenameString;

    bool opened = ImGui::TreeNodeEx(label.c_str(), flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selected_directory_ = directoryPath;
    }

    if (opened && has_subdirs) {
        for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
            if (entry.is_directory()) {
                DrawDirectoryTree(entry.path());
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void FileBrowserPanel::DrawDirectoryContent() {
    if (!std::filesystem::exists(selected_directory_)) {
        selected_directory_ = root_directory_;
    }

    if (selected_directory_ != root_directory_) {
        if (ImGui::Button("<- Back")) {
            selected_directory_ = selected_directory_.parent_path();
            renaming_path_.clear();  // Сбрасываем переименование при выходе из папки
        }
        ImGui::Separator();
    }

    float padding = 16.0f;
    float thumbnail_size = 64.0f;
    float cell_size = thumbnail_size + padding;

    float panel_width = ImGui::GetContentRegionAvail().x;
    int column_count = static_cast<int>(panel_width / cell_size);
    if (column_count < 1)
        column_count = 1;

    if (ImGui::BeginTable("ContentTable", column_count)) {
        for (const auto& entry : std::filesystem::directory_iterator(selected_directory_)) {
            const auto& path = entry.path();
            if (path.extension() == ".meta")
                continue;

            ImGui::TableNextColumn();
            ImGui::PushID(path.string().c_str());

            std::string filename_string = path.filename().string();
            bool is_selected = (selection_manager_.GetSelectedAsset() == path);

            ImGui::BeginGroup();

            if (is_selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
            }

            // --- Отрисовка кнопки (Папка/Файл) ---
            if (entry.is_directory()) {
                ImGui::Button("[DIR]", ImVec2(thumbnail_size, thumbnail_size));
                if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    selection_manager_.Select(path);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selected_directory_ = path;
                    renaming_path_.clear();
                }
            } else {
                ImGui::Button("[FILE]", ImVec2(thumbnail_size, thumbnail_size));
                if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    selection_manager_.Select(path);
                }

                // Двойной клик — открытие сцены по GUID из meta-файла
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (path.extension() == ".scene") {
                        std::filesystem::path meta_path = path.string() + ".meta";
                        if (std::filesystem::exists(meta_path)) {
                            try {
                                auto header = MetaSerializer::ReadHeader(meta_path);
                                // Предполагается, что GUID имеет тип uint64_t
                                scene_manager_.LoadScene(header->guid);
                            } catch (const std::exception& e) {
                                // Здесь можно залогировать ошибку чтения меты
                                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Meta Error: %s", e.what());
                            }
                        }
                    }
                }

                // Drag & Drop логика
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    try {
                        if (!std::filesystem::exists(path)) {
                            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: File not found");
                            throw std::runtime_error("File not found");
                        }

                        std::filesystem::path relative_path =
                            std::filesystem::relative(path, std::filesystem::current_path());
                        std::string itemPath = relative_path.generic_string();

                        std::filesystem::path meta_path = path.string() + ".meta";
                        if (!std::filesystem::exists(meta_path)) {
                            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Missing .meta file");
                        } else {
                            auto header = MetaSerializer::ReadHeader(meta_path);
                            AssetPayload payload{};
                            payload.asset_id = header->guid;
                            payload.expected_asset_type = entt::hashed_string{header->asset_type.c_str()}.value();

                            ImGui::SetDragDropPayload("ASSET_BROWSER_ITEM", &payload, sizeof(AssetPayload));

                            ImGui::Text("Asset: %s", path.filename().string().c_str());
                            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Type: %s", header->asset_type.c_str());
                            ImGui::Separator();
                            ImGui::TextDisabled("ID: %llu", header->guid);
                        }
                    } catch (const std::exception& e) {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", e.what());
                    } catch (...) {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Unknown critical error");
                    }
                    ImGui::EndDragDropSource();
                }
            }

            if (is_selected)
                ImGui::PopStyleColor();

            // --- Логика переименования ---
            // Горячая клавиша F2 для переименования выбранного элемента
            if (is_selected && ImGui::IsKeyPressed(ImGuiKey_F2) && renaming_path_ != path) {
                renaming_path_ = path;
                strncpy(rename_buffer_, filename_string.c_str(), sizeof(rename_buffer_));
                set_focus_to_rename_ = true;
            }

            if (renaming_path_ == path) {
                if (set_focus_to_rename_) {
                    ImGui::SetKeyboardFocusHere();
                    set_focus_to_rename_ = false;
                }

                ImGui::PushItemWidth(thumbnail_size);

                // Флаги: Применяем по Enter, выделяем текст при старте
                ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;

                if (ImGui::InputText("##rename", rename_buffer_, sizeof(rename_buffer_), flags)) {
                    std::string new_name = rename_buffer_;
                    std::filesystem::path new_path = path.parent_path() / new_name;

                    // Проверяем, что имя изменилось и файла с таким именем еще нет
                    if (!new_name.empty() && new_name != filename_string && !std::filesystem::exists(new_path)) {
                        try {
                            // 1. Переименовываем сам файл/папку
                            std::filesystem::rename(path, new_path);

                            // 2. Если это файл, переименовываем его .meta
                            if (!entry.is_directory()) {
                                std::filesystem::path old_meta = path.string() + ".meta";
                                std::filesystem::path new_meta = new_path.string() + ".meta";
                                if (std::filesystem::exists(old_meta)) {
                                    std::filesystem::rename(old_meta, new_meta);
                                }
                            }

                            // 3. Обновляем контекст редактора, если файл был выделен
                            if (selection_manager_.GetSelectedAsset() == path) {
                                selection_manager_.Select(new_path);
                            }

                            import_system_.Refresh();
                        } catch (const std::exception& e) {
                            // TODO: Здесь можно добавить вывод ошибки в консоль движка
                        }
                    }
                    renaming_path_.clear();  // Завершаем режим переименования
                }
                ImGui::PopItemWidth();

                // Отмена переименования по Escape
                if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    renaming_path_.clear();
                }

            } else {
                // Стандартная отрисовка текста
                if (is_selected) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", filename_string.c_str());
                } else {
                    ImGui::TextWrapped("%s", filename_string.c_str());
                }
            }

            ImGui::EndGroup();

            // --- Контекстное меню элемента ---
            if (ImGui::BeginPopupContextItem("ItemContextMenu")) {
                if (ImGui::MenuItem("Rename", "F2")) {
                    renaming_path_ = path;
                    strncpy(rename_buffer_, filename_string.c_str(), sizeof(rename_buffer_));
                    set_focus_to_rename_ = true;
                }
                if (ImGui::MenuItem("Delete", "Del")) {
                    if (entry.is_directory()) {
                        import_system_.DeleteDirectory(path);
                    } else {
                        import_system_.DeleteAsset(path);
                    }
                    import_system_.Refresh();

                    if (selection_manager_.GetSelectedAsset() == path) {
                        selection_manager_.ClearSelection();
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::IsMouseReleased(0) && ImGui::IsWindowHovered()) {
        if (!ImGui::IsAnyItemHovered()) {
            selection_manager_.ClearSelection();
            // Клик в пустое место отменяет переименование
            if (!renaming_path_.empty()) {
                renaming_path_.clear();
            }
        }
    }

    // ... (контекстное меню окна ContentBrowserContext остается без изменений) ...
    if (ImGui::BeginPopupContextWindow("ContentBrowserContext",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu("Create")) {
            for (auto* factory : factory_manager_.GetFactories()) {
                if (ImGui::MenuItem(factory->GetActionName().c_str())) {
                    factory->CreateDefault(selected_directory_);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}

}  // namespace tryeditor
