#include "editor/EditorApp.hpp"

#include <imgui_impl_sdl3.h>

#include "editor/Editor.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/GpuMeshLoader.hpp"
#include "engine/graphics/Renderer.hpp"
#include "engine/resources/GltfLoader.hpp"
#include "engine/resources/ResourceManager.hpp"
#include "engine/resources/Types.hpp"

namespace editor {
void EditorApp::Init() {
    engine = std::make_unique<engine::core::Engine>();
    graphicsContext = std::make_unique<engine::graphics::GraphicsContext>();

    if (!graphicsContext->Initialize(1280, 720, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return;
    }
    editor = std::make_unique<Editor>(*graphicsContext);

    engine::resources::ResourceManager resManager;

    using namespace entt::literals;

    resManager.RegisterAssetPath("player_mesh"_hs, "assets/models/player.gltf");
    resManager.RegisterAssetPath("level_config"_hs, "assets/configs/level1.json");

    resManager.RegisterCache<engine::graphics::Mesh>(engine::graphics::GpuMeshLoader(resManager, graphicsContext->GetDevice()));
    resManager.RegisterCache<engine::resources::MeshData>(engine::resources::GltfLoader(resManager));

    renderSystem = std::make_unique<engine::graphics::RenderSystem>(graphicsContext->GetDevice());


    editor->LoadGameLibrary("./libgame.so");
    engine->GetSceneManager().LoadScene("default.scene");


    editor->Running = true;
}

void EditorApp::Run() {
    while (editor->Running) {
        UpdateInput();

        engine->ProcessInput(inputState);
        engine->DispatchCommands();

        editor->editorCameraUpdate();
        // editor systems

        if (editor->PlayMode & editor->gameSO.IsValid()) {
            editor->gameSO.updateGameSystems(engine.get());
        }

        editor->RecordEditorGUI();

        editor->RenderEditorCamera();
        renderSystem->RenderScene();

        editor->RenderEditorGUI();
    }
}

void EditorApp::Shutdown() {

}
void EditorApp::UpdateInput() {
    inputState.ResetFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // 1. Отдаем событие ImGui в первую очередь
        ImGui_ImplSDL3_ProcessEvent(&event);

        switch (event.type) {
            case SDL_EVENT_QUIT: {
                // Закрываем редактор при нажатии на крестик окна
                editor->Running = false;
                // engine->PushCommand(CmdQuit{}); // Раскомментируй, если движку тоже надо знать о выходе
                break;
            }

            case SDL_EVENT_KEY_DOWN: {
                auto key = static_cast<engine::core::Key>(event.key.scancode);

                if (!event.key.repeat) {
                    inputState.justPressed[static_cast<int>(key)] = true;
                }
                inputState.isDown[static_cast<int>(key)] = true;
                break;
            }

            case SDL_EVENT_KEY_UP: {
                auto key = static_cast<engine::core::Key>(event.key.scancode);
                inputState.justReleased[static_cast<int>(key)] = true;
                inputState.isDown[static_cast<int>(key)] = false;
                break;
            }

            case SDL_EVENT_MOUSE_MOTION: {
                inputState.mouseX = event.motion.x;
                inputState.mouseY = event.motion.y;
                inputState.mouseDeltaX += event.motion.xrel;
                inputState.mouseDeltaY += event.motion.yrel;
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                // В SDL_BUTTON_LEFT = 1, поэтому вычитаем 1, чтобы попасть в индексы 0-4 нашего массива
                int btnIdx = event.button.button - 1;
                if (btnIdx >= 0 && btnIdx < 5) {
                    inputState.mouseButtons[btnIdx] = true;
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                int btnIdx = event.button.button - 1;
                if (btnIdx >= 0 && btnIdx < 5) {
                    inputState.mouseButtons[btnIdx] = false;
                }
                break;
            }

            case SDL_EVENT_MOUSE_WHEEL: {
                // Если добавишь float mouseScrollY в InputState, раскомментируй:
                // inputState.mouseScrollY = event.wheel.y;
                break;
            }
            default:;
        }
    }
}
}