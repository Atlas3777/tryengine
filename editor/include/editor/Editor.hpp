#pragma once
#include <memory>
#include <string>

#include "game/GameAPI.hpp"
#include "gui/EditorGUI.hpp"
#include "import/ImportSystem.hpp"

namespace editor {
class Spawner;

class Editor {
   public:
    Editor(engine::core::Engine& eng, engine::graphics::GraphicsContext& graphics_context, engine::graphics::RenderSystem& render_system);
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) noexcept = delete;
    Editor& operator=(Editor&&) noexcept = delete;

    ~Editor();

    bool LoadGameLibrary(const std::string& originalPath);
    void UnloadGameLibrary();

    void SaveScene();
    void SaveSceneForPlayMode();
    void LoadDefaultScene(engine::graphics::RenderSystem& render_system);

    void EditorCameraUpdate();

    void RegisterAssetsImporters() const;
    void RegisterResourceLoaders() const;

    EditorGUI& GetEditorGUI() { return *editor_gui_; }
    ImportSystem& GetImportSystem() { return *import_system_; }

    bool running = false;
    bool play_pode = false;
    GameLibrary gameSO;

   private:
    std::unique_ptr<ImportSystem> import_system_;
    std::unique_ptr<EditorGUI> editor_gui_;
    std::unique_ptr<Spawner> spawner_;

    engine::graphics::GraphicsContext& graphics_context_;
    engine::core::Engine& engine_;
};

}  // namespace editor