#pragma once
#include <filesystem>

#include "editor/EditorContext.hpp"
#include "editor/gui/IPanel.hpp"
#include "editor/import/ImportSystem.hpp"

namespace tryeditor {
class AssetsFactoryManager;

class FileBrowserPanel : public IPanel {
public:
    FileBrowserPanel(ImportSystem& import_system, EditorContext& editor_context, AssetsFactoryManager& factory_manager);
    const char* GetName() const override { return "FileBrowserPanel"; }

    void OnImGuiRender(entt::registry& reg) override;

private:
    void DrawDirectoryTree(const std::filesystem::path& directoryPath);
    void DrawDirectoryContent();

    enum class ContentMode { Game, Engine };
    ContentMode current_mode_ = ContentMode::Game;

    std::filesystem::path game_root_;
    std::filesystem::path engine_root_;

    ImportSystem& import_system_;
    EditorContext& editor_context_;
    AssetsFactoryManager& factory_manager_;

    std::filesystem::path root_directory_;
    std::filesystem::path selected_directory_;
};

} // namespace tryeditor