#pragma once
#include <memory>
#include <string>

#include "game/GameAPI.hpp"
#include "gui/EditorGUI.hpp"
#include "import/ImportSystem.hpp"

namespace tryeditor {
class Spawner;

class Editor {
   public:
    Editor(tryengine::core::Engine& eng, tryengine::graphics::GraphicsContext& graphics_context, tryengine::graphics::RenderSystem& render_system);
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) noexcept = delete;
    Editor& operator=(Editor&&) noexcept = delete;

    ~Editor();

    bool LoadGameLibrary(const std::string& originalPath);
    void UnloadGameLibrary();

    void SaveScene();
    void SaveSceneForPlayMode();
    void LoadDefaultScene() const;


    void RegisterAssetsImporters() const;
    void RegisterResourceLoaders() const;

    EditorGUI& GetEditorGUI() { return *editor_gui_; }
    ImportSystem& GetImportSystem() { return *import_system_; }

    bool running = false;
    bool play_mode = false;
    GameLibrary gameSO;

   private:
    std::unique_ptr<ImportSystem> import_system_;
    std::unique_ptr<EditorGUI> editor_gui_;
    std::unique_ptr<Spawner> spawner_;

    tryengine::graphics::GraphicsContext& graphics_context_;
    tryengine::core::Engine& engine_;
};

}  // namespace tryeditor