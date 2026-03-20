#pragma once
#include "editor/EditorGUI.hpp"
#include "game/GameAPI.hpp"

#include <memory>
#include <string>

namespace editor {

class Editor {
public:
    Editor(engine::GraphicsContext& graphics_context);
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) noexcept = delete;
    Editor& operator=(Editor&&) noexcept = delete;

    bool Running = false;
    bool PlayMode = false;

    // Храним состояние загруженной библиотеки
    GameLibrary gameSO;

    void RecordEditorGUI();
    void RenderEditorGUI();


    // Возвращаем bool для проверки успеха загрузки
    bool LoadGameLibrary(const std::string& originalPath);
    void UnloadGameLibrary();

    void SaveScene();
    void SaveSceneForPlayMode();

    // Методы из твоего EditorApp (добавлены для консистентности)
    void editorCameraUpdate();
    void RenderEditorCamera();

private:
    std::unique_ptr<EditorGUI> editorGUI;
};

} // namespace editor