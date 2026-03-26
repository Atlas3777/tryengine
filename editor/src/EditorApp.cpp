#include "editor/EditorApp.hpp"

#include <imgui_impl_sdl3.h>

#include "editor/BaseSystem.hpp"
#include "editor/Components.hpp"
#include "editor/Editor.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/GpuMeshLoader.hpp"
#include "engine/graphics/Renderer.hpp"
#include "engine/resources/AssetDatabase.hpp"
#include "engine/resources/GltfLoader.hpp"
#include "engine/resources/ResourceManager.hpp"
#include "engine/resources/Types.hpp"

namespace editor {
void EditorApp::Init() {
    engine = std::make_unique<engine::core::Engine>();
    graphics_context_ = std::make_unique<engine::graphics::GraphicsContext>();

    if (!graphics_context_->Initialize(1280, 720, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return;
    }
    editor = std::make_unique<Editor>(*graphics_context_);

    using namespace entt::literals;

    engine::resources::ResourceManager resManager;
    engine::resources::AssetDatabase assetDatabase;
    //
    // resManager.RegisterCache<engine::resources::MeshData>(engine::resources::GltfLoader(resManager));
    // resManager.RegisterCache<engine::graphics::Mesh>(engine::graphics::GpuMeshLoader(resManager,
    // graphicsContext->GetDevice()));

    renderSystem = std::make_unique<engine::graphics::RenderSystem>(graphics_context_->GetDevice());

    editor->LoadGameLibrary("build/game/libgame.so");
    // editor->LoadDefaultScene();
    engine->GetSceneManager().LoadScene("default.scene");

    auto& registry = engine->GetSceneManager().GetActiveScene()->GetRegistry();

    auto editorCamera = registry.create();
    registry.emplace<engine::Tag>(editorCamera, "EditorCamera");
    registry.emplace<engine::Transform>(editorCamera,
    engine::Transform{glm::vec3(0.f, 0.f, -2.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<engine::Camera>(editorCamera);
    registry.emplace<EditorCameraTag>(editorCamera);

    auto gameCamera = registry.create();
    registry.emplace<engine::Tag>(gameCamera, "GameCamera");
    registry.emplace<engine::Transform>(gameCamera,
    engine::Transform{glm::vec3(0.f, 0.f, -2.f), glm::quat(), glm::vec3(1.f)});
    registry.emplace<engine::Camera>(gameCamera);
    registry.emplace<engine::MainCameraTag>(gameCamera);

    editor->Running = true;
}

void EditorApp::Run() {
    while (editor->Running) {
        engine->GetClock().Update();
        UpdateInput();

        engine->ProcessInput(inputState);
        // engine->DispatchCommands();

        UpdateEditorCameraSystem(engine->GetSceneManager().GetActiveScene()->GetRegistry(), engine->GetClock().GetDeltaTime(), *(engine->input));
        // editor systems

        if (editor->PlayMode & editor->gameSO.IsValid()) {
            editor->gameSO.updateGameSystems(engine.get());
        }

        editor->GetEditorGUI().RecordPanelsGpuCommands(*engine);

        const auto cmd = SDL_AcquireGPUCommandBuffer(graphics_context_->GetDevice());

        SDL_GPUTexture* swapchainTexture = nullptr;
        uint32_t w, h;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, graphics_context_->GetWindow(), &swapchainTexture, &w, &h)) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }
        editor->GetEditorGUI().RenderToPanel(cmd, *renderSystem, *engine);

        editor->GetEditorGUI().RenderPanelsToSwapchain(swapchainTexture, cmd);
    }
}

void EditorApp::Shutdown() {}
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
}  // namespace editor