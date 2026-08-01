#include "editor/EditorApp.hpp"

#include <imgui_impl_sdl3.h>

#include "editor/AppBootstrap.hpp"
#include "editor/AssetSourceDatabase.hpp"
#include "editor/BaseSystem.hpp"
#include "editor/Editor.hpp"
#include "editor/InputMapper.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "editor/gui/SceneViewportPanel.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/BaseSystem.hpp"
#include "engine/core/ComponentRegistry.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputService.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/SceneManager.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/Time.hpp"
#include "engine/resources/ReadFullFile.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryeditor {

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

using namespace tryengine::core;
using namespace tryengine::graphics;
using namespace tryengine::resources;
using namespace tryengine::async;

void EditorApp::Init() {
    graphics_context_ = std::make_unique<GraphicsContext>(1280, 720, "tryengine");
    render_system_ = std::make_unique<RenderSystem>(graphics_context_->GetDevice());
    engine_ = std::make_unique<Engine>();

    engine_->RegisterSystem<Time>();
    engine_->RegisterSystem<SceneManager>();
    engine_->RegisterSystem<InputService>(this->input_state_);
    engine_->RegisterSystem<ScriptSystem>("./editor/daslang/EntryPoint.das");
    engine_->RegisterSystem<ResourceManager>();

    editor_ = std::make_unique<Editor>(*engine_, *graphics_context_);

    editor_->LoadDefaultScene();
    engine_->Get<ScriptSystem>().InvokeStart();

    auto task = RunPollable(ThreadPool(),editor_->GetAssetSourceDatabase().InitEngineContentSync(async_file_manager_));
    while (!task.IsReady())
    {
        async_file_manager_.Pull();
        MainThread().Pull();
        async_file_manager_.Submit();
        std::this_thread::yield();
    }

    RunAndForget(ThreadPool(), editor_->GetAssetSourceDatabase().AsyncLoadGameContent(async_file_manager_));
}

void EditorApp::Run() {
    editor_->running = true;
    editor_->play_mode = false;

    auto& time = engine_->Get<Time>();

    while (editor_->running) {
        TRY_PROFILE_SCOPE("Main Loop Frame");
        {
            TRY_PROFILE_SCOPE("Render Less Frame");
            UpdateInput();
            time.NewFrame();
            async_file_manager_.Pull();
            MainThread().Pull();

            auto& reg = engine_->Get<SceneManager>().GetActiveScene().GetRegistry();
            auto& panel = *editor_->GetGUI().GetSceneViewportPanel();
            if (panel.is_input_captured_ && (panel.is_focused_ || panel.is_hovered_)) {
                UpdateEditorCameraSystem(reg, time.DeltaTime(), input_state_);
            }

            UpdateTransformSystem(reg);
            UpdateCameraMatrices(reg);

            engine_->Get<ScriptSystem>().CheckForReload(time.UnscaledDeltaTime());

            if (editor_->play_mode)
                engine_->Get<ScriptSystem>().InvokeUpdate(time.DeltaTime());

            async_file_manager_.Submit();

            editor_->GetGUI().RecordPanelsGpuCommands(*engine_, editor_->play_mode);
        }

        {
            TRY_PROFILE_SCOPE("Render GPU");
            const auto cmd = SDL_AcquireGPUCommandBuffer(graphics_context_->GetDevice());

            SDL_GPUTexture* swapchainTexture = nullptr;
            uint32_t w, h;
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, graphics_context_->GetWindow(), &swapchainTexture, &w,
                                                       &h)) {
                SDL_SubmitGPUCommandBuffer(cmd);
                continue;
            }

            editor_->GetGUI().RenderToPanel(cmd, *render_system_, *engine_);
            editor_->GetGUI().RenderPanelsToSwapchain(swapchainTexture, cmd);
        }
        Profiler::Instance().EndFrame(time.DeltaTime() * 1000.0f);
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
