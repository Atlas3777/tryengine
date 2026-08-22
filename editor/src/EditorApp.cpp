#include "editor/EditorApp.hpp"

#include <engine/graphics/RenderSystem.hpp>
#include <imgui_impl_sdl3.h>

#include "editor/AssetSourceDatabase.hpp"
#include "editor/CreateFile.hpp"
#include "editor/Editor.hpp"
#include "editor/InputMapper.hpp"
#include "editor/TryEditorContext.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputService.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/Time.hpp"
#include "engine/graphics/RenderAdapter.hpp"
#include "engine/graphics/RenderCommon.hpp"
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

    engine_ = std::make_unique<Engine>();
    engine_->RegisterSystem<RenderSystem>(graphics_context_->GetDevice());
    engine_->RegisterSystem<RenderAdapter>();
    engine_->RegisterSystem<Time>();
    engine_->RegisterSystem<InputService>(this->input_state_);
    engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<ResourceManager>(async_file_manager_, engine_->Get<AssetRegistry>());

    auto& script_system = engine_->RegisterSystem<ScriptSystem>(*engine_);
    editor_ = std::make_unique<Editor>(*engine_, *graphics_context_);

    script_system.SetContextFactory([this](Engine& eng, uint32_t stack_size) -> das::Context* {
        return new TryEditorContext(eng, *editor_, stack_size);
    });

    script_system.LoadMainScript("./editor/daslang/EntryPoint.das");

    editor_->LoadDefaultScene();

    const auto task =
        RunPollable(ThreadPool(), editor_->GetAssetSourceDatabase().InitEngineContentSync(async_file_manager_));

    while (!task.IsReady()) {
        async_file_manager_.Pull();
        MainThread().Pull();
        async_file_manager_.Submit();
        std::this_thread::yield();
    }

    engine_->Get<ScriptSystem>().InvokeStart();

    RunAndForget(ThreadPool(), editor_->GetAssetSourceDatabase().AsyncLoadGameContent(async_file_manager_));
}

void EditorApp::Run() {
    editor_->running = true;
    editor_->play_mode = false;
    auto& render_system = engine_->Get<RenderSystem>();

    auto& time = engine_->Get<Time>();

    while (editor_->running) {
        TRY_PROFILE_SCOPE("Main Loop Frame");
        {
            TRY_PROFILE_SCOPE("Render Less Frame");
            UpdateInput();

            time.NewFrame();
            async_file_manager_.Pull();
            MainThread().Pull();

            engine_->Get<ScriptSystem>().CheckForReload(time.UnscaledDeltaTime());

            engine_->Get<ScriptSystem>().InvokeFunctionFast("EditorUpdate", time.UnscaledDeltaTime());

            if (editor_->play_mode)
                engine_->Get<ScriptSystem>().InvokeUpdate(time.ScaledTotalTime());

            async_file_manager_.Submit();

            editor_->GetGUI().RecordPanelsGpuCommands(editor_->play_mode);
        }

        {
            TRY_PROFILE_SCOPE("Render GPU");
            const auto cmd = SDL_AcquireGPUCommandBuffer(graphics_context_->GetDevice());

            SDL_GPUTexture* swapchain_texture = nullptr;
            uint32_t w, h;
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, graphics_context_->GetWindow(), &swapchain_texture, &w,
                                                       &h)) {
                SDL_SubmitGPUCommandBuffer(cmd);
                continue;
            }

            engine_->Get<RenderAdapter>().CollectDrawable(*engine_, render_system);
            auto camera_data = engine_->Get<ScriptSystem>().SimpleReturnUnsafe<CameraData*>("get_camera");
            render_system.RenderToTarget(cmd, *editor_->target, *(*camera_data));

            editor_->GetGUI().RenderToSwapchain(swapchain_texture, cmd);
        }
        Profiler::Instance().EndFrame(time.ScaledDeltaTime() * 1000.0f);
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
    LogInfo("EditorApp shutting down...");
}

}  // namespace tryeditor
