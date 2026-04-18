#include "editor/gui/FileBrowserPanel.hpp"

#include <cereal/archives/json.hpp>
#include <fstream>

#include "editor/EditorGUIUtils.hpp"
#include "editor/asset_factories/AssetsFactoryManager.hpp"
#include "editor/import/ImportSystem.hpp"
#include "engine/core/AssetDatabase.hpp"

namespace tryeditor {

FileBrowserPanel::FileBrowserPanel(ImportSystem& import_system, EditorContext& editor_context,
                                   AssetsFactoryManager& factory_manager)
    : import_system_(import_system), editor_context_(editor_context), factory_manager_(factory_manager) {
    root_directory_ = std::filesystem::current_path() / "game/assets/";

    if (!std::filesystem::exists(root_directory_)) {
        std::filesystem::create_directories(root_directory_);
    }

    selected_directory_ = root_directory_;
}

void FileBrowserPanel::OnImGuiRender(entt::registry& reg) {
    ImGui::Begin("Project Tree");
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

    // Подсветка выбранной папки
    if (selected_directory_ == directoryPath) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Проверяем наличие вложенных папок
    bool has_subdirs = false;
    for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
        if (entry.is_directory()) {
            has_subdirs = true;
            break;
        }
    }

    // Если папок внутри нет — рисуем как лист (без стрелочки)
    if (!has_subdirs) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    ImGui::PushID(directoryPath.string().c_str());

    // Используем иконку-заглушку. Если есть FontAwesome, замени на ICON_FA_FOLDER
    const char* icon = has_subdirs ? "(+) " : "( ) ";
    std::string label = icon + filenameString;

    bool opened = ImGui::TreeNodeEx(label.c_str(), flags);

    // Логика выделения: клик по строке, но не по стрелке развертывания
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
            if (path.extension() == ".meta") continue;

            ImGui::TableNextColumn();
            ImGui::PushID(path.string().c_str());

            std::string filename_string = path.filename().string();
            bool is_selected = (editor_context_.selected_asset_path == path);

            // Используем группу, чтобы контекстное меню привязывалось к ячейке целиком
            ImGui::BeginGroup();

            if (is_selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
            }

            if (entry.is_directory()) {
                ImGui::Button("[DIR]", ImVec2(thumbnail_size, thumbnail_size));
                if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    editor_context_.SetActive(path);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selected_directory_ = path;
                }
            } else {
                ImGui::Button("[FILE]", ImVec2(thumbnail_size, thumbnail_size));
                if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    editor_context_.SetActive(path);
                }

                // --- Drag & Drop ---
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    try {
                        // 1. Проверка существования самого ассета
                        if (!std::filesystem::exists(path)) {
                            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: File not found");
                            throw std::runtime_error("File not found");
                        }

                        std::filesystem::path relative_path =
                            std::filesystem::relative(path, std::filesystem::current_path());
                        std::string itemPath = relative_path.generic_string();

                        // 2. Получение ID (может кинуть исключение, если путь не зарегистрирован)
                        uint64_t id = import_system_.GetId(itemPath);
                        if (id == 0) {  // Если ваша система возвращает 0 при ошибке
                            ImGui::TextDisabled("Status: Not registered in DB");
                        }

                        // 3. Проверка мета-файла
                        std::filesystem::path metaPath = path.string() + ".meta";
                        if (!std::filesystem::exists(metaPath)) {
                            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Missing .meta file");
                        } else {
                            auto meta_header = import_system_.PeekMetaHeader(metaPath);

                            if (meta_header) {
                                AssetPayload payload;
                                payload.asset_id = id;
                                payload.expected_asset_type =
                                    entt::hashed_string{meta_header->asset_type.c_str()}.value();

                                ImGui::SetDragDropPayload("ASSET_BROWSER_ITEM", &payload, sizeof(AssetPayload));

                                // Визуальная подсказка
                                ImGui::Text("Asset: %s", path.filename().string().c_str());
                                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Type: %s",
                                                   meta_header->asset_type.c_str());
                                ImGui::Separator();
                                ImGui::TextDisabled("ID: %llu", id);
                            } else {
                                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error: Invalid meta header");
                            }
                        }
                    } catch (const std::filesystem::filesystem_error& e) {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "FS Error: %s", e.what());
                    } catch (const std::exception& e) {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", e.what());
                    } catch (...) {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Unknown critical error");
                    }

                    ImGui::EndDragDropSource();
                }
            }

            if (is_selected) ImGui::PopStyleColor();

            // Вывод имени (ОДИН РАЗ)
            if (is_selected) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", filename_string.c_str());
            } else {
                ImGui::TextWrapped("%s", filename_string.c_str());
            }

            ImGui::EndGroup();

            // Контекстное меню привязано к группе выше
            if (ImGui::BeginPopupContextItem("ItemContextMenu")) {
                if (ImGui::MenuItem("Delete", "Del")) {
                    if (entry.is_directory()) {
                        import_system_.DeleteDirectory(path);
                    } else {
                        import_system_.DeleteAsset(path);
                    }
                    // Важно: вызываем обновление системы после удаления
                    import_system_.Refresh();

                    if (editor_context_.selected_asset_path == path) {
                        editor_context_.selected_asset_path.clear();
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Сброс выделения при клике на пустое место (тоже на отпускание)
    if (ImGui::IsMouseReleased(0) && ImGui::IsWindowHovered()) {
        if (!ImGui::IsAnyItemHovered()) {
            editor_context_.selected_asset_path.clear();
        }
    }

    if (ImGui::BeginPopupContextWindow("ContentBrowserContext",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu("Create")) {
            // Динамически получаем все фабрики и выводим их в список
            for (auto* factory : factory_manager_.GetFactories()) {
                if (ImGui::MenuItem(factory->GetActionName().c_str())) {
                    // Вызываем дефолтное создание в выбранной папке
                    factory->CreateDefault(selected_directory_);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}

}  // namespace tryeditor