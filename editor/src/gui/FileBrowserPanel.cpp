#include "editor/gui/FileBrowserPanel.hpp"

#include "editor/import/ImportSystem.hpp"
#include "engine/core/AssetDatabase.hpp"

namespace tryeditor {

FileBrowserPanel::FileBrowserPanel(ImportSystem& import_system) : import_system_(import_system) {
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

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_OpenOnDoubleClick
                             | ImGuiTreeNodeFlags_SpanAvailWidth;

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
    float thumbnail_size = 64.0f;  // Размер иконки
    float cell_size = thumbnail_size + padding;

    float panel_width = ImGui::GetContentRegionAvail().x;
    int column_count = static_cast<int>(panel_width / cell_size);
    if (column_count < 1)
        column_count = 1;

    if (ImGui::BeginTable("ContentTable", column_count)) {
        for (const auto& entry : std::filesystem::directory_iterator(selected_directory_)) {
            const auto& path = entry.path();

            if (path.extension() == ".meta") {
                continue;
            }

            ImGui::TableNextColumn();

            std::string filename_string = path.filename().string();

            // PushID для предотвращения конфликтов в правой панели
            ImGui::PushID(path.string().c_str());

            if (entry.is_directory()) {
                // Заглушка для папки
                ImGui::Button("[DIR]", ImVec2(thumbnail_size, thumbnail_size));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selected_directory_ = path;
                }
            } else {
                // Заглушка для файла
                ImGui::Button("[FILE]", ImVec2(thumbnail_size, thumbnail_size));

                // --- ЛОГИКА DRAG & DROP ---
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {

                    std::filesystem::path relative_path = std::filesystem::relative(path, std::filesystem::current_path());
                    std::string itemPath = relative_path.generic_string();

                    try {
                        uint64_t id = import_system_.GetId(itemPath);
                        ImGui::SetDragDropPayload("CONTENT_BROWSER_ID", &id, sizeof(uint64_t));
                        ImGui::Text("ID: %llu", id);
                    } catch (const std::out_of_range& e) {
                        ImGui::TextDisabled("Not imported: %s", filename_string.c_str());
                    }

                    ImGui::Text("Spawning: %s", filename_string.c_str());
                    ImGui::EndDragDropSource();
                }
            }

            // Текст под иконкой
            ImGui::TextWrapped("%s", filename_string.c_str());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace tryeditor