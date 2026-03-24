#pragma once
#include <memory>
#include <string>

#include "editor/EditorGUI.hpp"
#include "game/GameAPI.hpp"

namespace editor {

class Editor {
   public:
    Editor(engine::graphics::GraphicsContext& graphics_context) {
        editorGUI = std::make_unique<EditorGUI>(graphics_context);
    };
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) noexcept = delete;
    Editor& operator=(Editor&&) noexcept = delete;

    bool Running = false;
    bool PlayMode = false;

    // Храним состояние загруженной библиотеки
    GameLibrary gameSO;

    // Возвращаем bool для проверки успеха загрузки
    bool LoadGameLibrary(const std::string& originalPath);
    void UnloadGameLibrary();

    void SaveScene();
    void SaveSceneForPlayMode();

    // Методы из твоего EditorApp (добавлены для консистентности)
    void editorCameraUpdate();
    EditorGUI& GetEditorGUI() { return *editorGUI; };

   private:
    std::unique_ptr<EditorGUI> editorGUI;
};

}  // namespace editor