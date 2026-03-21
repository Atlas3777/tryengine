#include "editor/EditorApp.hpp"

#include <entt/entt.hpp>

#include "editor/Editor.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/Renderer.hpp"

namespace editor {
void EditorApp::Init() {
    engine = std::make_unique<engine::core::Engine>();
    graphicsContext = std::make_unique<engine::graphics::GraphicsContext>();

    if (!graphicsContext->Initialize(1280, 720, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return;
    }
    editor = std::make_unique<Editor>(*graphicsContext);


    target = std::make_unique<engine::graphics::RenderTarget>(graphicsContext->GetDevice(), 1280, 720,
                                                    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

    editor->LoadGameLibrary("./libgame.so");
    engine->GetSceneManager().LoadScene("default.scene");
    engine::Renderer renderer;
    renderer.Init(graphicsContext->GetDevice());

    editor->Running = true;
}

void EditorApp::Run() {
    while (editor->Running) {
        engine->ProcessInput();
        engine->DispatchCommands();

        editor->editorCameraUpdate();
        //editor systems

        if (editor->PlayMode & editor->gameSO.IsValid()) {
            editor->gameSO.updateGameSystems(engine.get());
        }

        editor->RecordEditorGUI();

        editor->RenderEditorCamera();
        engine->BaseRender();

        editor->RenderEditorGUI();
    }
}

void EditorApp::Shutdown() {

}
}  // namespace editor
