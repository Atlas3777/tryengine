#include "editor/gui/FileBrowserPanel.hpp"

#include "editor/import/ImportSystem.hpp"
#include "engine/core/AssetDatabase.hpp"

namespace editor {

FileBrowserPanel::FileBrowserPanel(ImportSystem& import_system) : import_system_(import_system) {
    root_directory_ = std::filesystem::current_path() / "game/assets/";

    if (!std::filesystem::exists(root_directory_)) {
        std::filesystem::create_directories(root_directory_);
    }
    selected_directory_ = root_directory_;
}

void FileBrowserPanel::OnImGuiRender(entt::registry& reg) {
    // --- ПЕРВОЕ ОКНО: Дерево папок ---
    ImGui::Begin("Project Tree");
    DrawDirectoryTree(root_directory_);
    ImGui::End();

    // --- ВТОРОЕ ОКНО: Содержимое папки (Сетка) ---
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

    std::string pathStr = directoryPath.string();
    ImGui::PushID(pathStr.c_str());

    // Рисуем узел. В качестве имени используем filenameString
    bool opened = ImGui::TreeNodeEx(filenameString.c_str(), flags);

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

    // --- Настройка сетки (Grid) ---
    float padding = 16.0f;
    float thumbnailSize = 64.0f;  // Размер иконки
    float cellSize = thumbnailSize + padding;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = (int) (panelWidth / cellSize);
    if (columnCount < 1)
        columnCount = 1;

    if (ImGui::BeginTable("ContentTable", columnCount)) {
        for (const auto& entry : std::filesystem::directory_iterator(selected_directory_)) {
            ImGui::TableNextColumn();

            const auto& path = entry.path();
            std::string filenameString = path.filename().string();

            // PushID для предотвращения конфликтов в правой панели
            ImGui::PushID(path.string().c_str());

            if (entry.is_directory()) {
                // Заглушка для папки
                ImGui::Button("[DIR]", ImVec2(thumbnailSize, thumbnailSize));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selected_directory_ = path;
                }
            } else {
                // Заглушка для файла
                ImGui::Button("[FILE]", ImVec2(thumbnailSize, thumbnailSize));

                // --- ЛОГИКА DRAG & DROP ---
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {

                    // 1. Получаем путь относительно корня проекта (папки game)
                    // root_path_ в ImportSystem — это current_path() / "game"
                    // Нам нужно получить путь вида "game/assets/..."

                    std::filesystem::path relativePath = std::filesystem::relative(path, std::filesystem::current_path());
                    std::string itemPath = relativePath.generic_string(); // Используем generic_string для кроссплатформенных слешей

                    try {
                        uint64_t id = import_system_.GetId(itemPath);
                        ImGui::SetDragDropPayload("CONTENT_BROWSER_ID", &id, sizeof(uint64_t));
                        ImGui::Text("ID: %llu", id);
                    } catch (const std::out_of_range& e) {
                        ImGui::TextDisabled("Not imported: %s", filenameString.c_str());
                    }

                    ImGui::Text("Spawning: %s", filenameString.c_str());
                    ImGui::EndDragDropSource();
                }
            }

            // Текст под иконкой
            ImGui::TextWrapped("%s", filenameString.c_str());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace editor