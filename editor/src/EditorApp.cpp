#include "editor/EditorApp.hpp"

#include <imgui_impl_sdl3.h>

#include "editor/Editor.hpp"
#include "editor/InputMapper.hpp"
#include "editor/Reflection.hpp"
#include "engine/core/AssetDatabase.hpp"
#include "engine/core/BaseSystem.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/ResourceManager.hpp"

namespace tryeditor {
void EditorApp::Init() {
    engine_ = std::make_unique<tryengine::core::Engine>();
    engine_->SetInputSource(&(this->input_state_));

    graphics_context_ = std::make_unique<tryengine::graphics::GraphicsContext>();
    if (!graphics_context_->Initialize(1280, 720, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return;
    }

    render_system_ = std::make_unique<tryengine::graphics::RenderSystem>(graphics_context_->GetDevice());

    editor_ = std::make_unique<Editor>(*engine_, *graphics_context_, *render_system_);

    editor_->RegisterAssetsImporters();
    editor_->RegisterResourceLoaders();
    editor_->RegisterAssetsFactories();
    editor_->RegisterAssetsInspector();

    editor_->GetImportSystem().Refresh();

    engine_->GetResourceManager().GetAssetDatabase().Refresh();

    editor_->LoadGameLibrary("build/game/libgame.so");
    editor_->LoadDefaultScene();

    RegisterRef();

    editor_->running = true;
}

void EditorApp::Run() {
    while (editor_->running) {
        UpdateInput();
        engine_->GetClock().Update();

        editor_->GetEditorGUI().UpdatePanels(*engine_);

        tryengine::core::UpdateTransformSystem(engine_->GetSceneManager().GetActiveScene()->GetRegistry());
        tryengine::core::UpdateCameraMatrices(engine_->GetSceneManager().GetActiveScene()->GetRegistry());

        if (editor_->play_mode & editor_->gameSO.IsValid()) {
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

void EditorApp::UpdateInput() {
    input_state_.ResetFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT) {
            editor_->running = false;
            continue;
        }

        InputMapper::ProcessEvent(event, input_state_);
    }
}

void EditorApp::Shutdown() {
    std::cout << "EditorApp shutting down..." << std::endl;
}
}  // namespace tryeditor
