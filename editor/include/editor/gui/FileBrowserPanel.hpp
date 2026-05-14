#pragma once
#include <filesystem>

#include "editor/SelectionManager.hpp"
#include "editor/gui/IPanel.hpp"
#include "editor/import/ImportSystem.hpp"

namespace tryengine::core {
class SceneManager;
}
namespace tryeditor {
class SceneManagerController;
class AssetsFactoryManager;

class FileBrowserPanel : public IPanel {
public:
    FileBrowserPanel(ImportSystem& import_system, SelectionManager& selection_manager,
                     AssetsFactoryManager& factory_manager, tryengine::core::SceneManager& scene_manager_);
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
    SelectionManager& selection_manager_;
    AssetsFactoryManager& factory_manager_;
    tryengine::core::SceneManager& scene_manager_;

    std::filesystem::path root_directory_;
    std::filesystem::path selected_directory_;

    std::filesystem::path renaming_path_;
    char rename_buffer_[256] = "";
    bool set_focus_to_rename_ = false;
};

}  // namespace tryeditor
