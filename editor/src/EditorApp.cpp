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
    engine_ = std::make_unique<engine::core::Engine>();
    graphics_context_ = std::make_unique<engine::graphics::GraphicsContext>();

    if (!graphics_context_->Initialize(1280, 720, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return;
    }
    editor_ = std::make_unique<Editor>(*engine_, *graphics_context_);
    render_system_ = std::make_unique<engine::graphics::RenderSystem>(graphics_context_->GetDevice());
    engine_->SetInputSource(&this->input_state_);

    using namespace entt::literals;

    engine::resources::ResourceManager resManager;
    engine::resources::AssetDatabase assetDatabase;

    // resManager.RegisterCache<engine::resources::MeshData>(engine::resources::GltfLoader(resManager));
    // resManager.RegisterCache<engine::graphics::Mesh>(engine::graphics::GpuMeshLoader(resManager,
    // graphicsContext->GetDevice()));

    editor_->LoadGameLibrary("build/game/libgame.so");
    editor_->LoadDefaultScene();

    editor_->Running = true;
}

void EditorApp::Run() {
    while (editor_->Running) {
        UpdateInput();
        engine_->GetClock().Update();

        // engine->DispatchCommands();

        UpdateEditorCameraSystem(engine_->GetSceneManager().GetActiveScene()->GetRegistry(),
                                 engine_->GetClock().GetDeltaTime(), engine_->GetInput());
        // editor systems

        if (editor_->PlayMode & editor_->gameSO.IsValid()) {
            editor_->gameSO.updateGameSystems(engine_.get());
        }

        editor_->GetEditorGUI().RecordPanelsGpuCommands(*engine_);

        const auto cmd = SDL_AcquireGPUCommandBuffer(graphics_context_->GetDevice());

        SDL_GPUTexture* swapchainTexture = nullptr;
        uint32_t w, h;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, graphics_context_->GetWindow(), &swapchainTexture, &w, &h)) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }
        editor_->GetEditorGUI().RenderToPanel(cmd, *render_system_, *engine_);

        editor_->GetEditorGUI().RenderPanelsToSwapchain(swapchainTexture, cmd);
    }
}

void EditorApp::Shutdown() {}
void EditorApp::UpdateInput() {
    input_state_.ResetFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // 1. Отдаем событие ImGui в первую очередь
        ImGui_ImplSDL3_ProcessEvent(&event);

        switch (event.type) {
            case SDL_EVENT_QUIT: {
                // Закрываем редактор при нажатии на крестик окна
                editor_->Running = false;
                // engine->PushCommand(CmdQuit{}); // Раскомментируй, если движку тоже надо знать о выходе
                break;
            }

            case SDL_EVENT_KEY_DOWN: {
                auto key = static_cast<engine::core::Key>(event.key.scancode);

                if (!event.key.repeat) {
                    input_state_.justPressed[static_cast<int>(key)] = true;
                }
                input_state_.isDown[static_cast<int>(key)] = true;
                break;
            }

            case SDL_EVENT_KEY_UP: {
                auto key = static_cast<engine::core::Key>(event.key.scancode);
                input_state_.justReleased[static_cast<int>(key)] = true;
                input_state_.isDown[static_cast<int>(key)] = false;
                break;
            }

            case SDL_EVENT_MOUSE_MOTION: {
                input_state_.mouseX = event.motion.x;
                input_state_.mouseY = event.motion.y;
                input_state_.mouseDeltaX += event.motion.xrel;
                input_state_.mouseDeltaY += event.motion.yrel;
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                // В SDL_BUTTON_LEFT = 1, поэтому вычитаем 1, чтобы попасть в индексы 0-4 нашего массива
                int btnIdx = event.button.button - 1;
                if (btnIdx >= 0 && btnIdx < 5) {
                    input_state_.mouseButtons[btnIdx] = true;
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                int btnIdx = event.button.button - 1;
                if (btnIdx >= 0 && btnIdx < 5) {
                    input_state_.mouseButtons[btnIdx] = false;
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