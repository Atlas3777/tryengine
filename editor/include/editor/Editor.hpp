#pragma once
#include <memory>
#include <string>

#include "editor/EditorGUI.hpp"
#include "game/GameAPI.hpp"

namespace editor {

class Editor {
   public:
    Editor(engine::core::Engine& eng, engine::graphics::GraphicsContext& graphics_context);
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) noexcept = delete;
    Editor& operator=(Editor&&) noexcept = delete;

    ~Editor();

    bool LoadGameLibrary(const std::string& originalPath);
    void UnloadGameLibrary();

    void SaveScene();
    void SaveSceneForPlayMode();
    void LoadDefaultScene();

    void editorCameraUpdate();

    EditorGUI& GetEditorGUI() { return *editorGUI; }

    bool Running = false;
    bool PlayMode = false;
    GameLibrary gameSO;

   private:
    engine::core::Engine& engine_;
    std::unique_ptr<EditorGUI> editorGUI;
};

}  // namespace editor