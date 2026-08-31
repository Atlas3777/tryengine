#include "editor/EditorApp.hpp"

#include <engine/graphics/RenderSystem.hpp>
#include <imgui_impl_sdl3.h>

#include "../../engine_source/graphics/include/engine/graphics/InputMapper.hpp"
#include "editor/AssetSourceDatabase.hpp"
#include "editor/CreateFile.hpp"
#include "editor/Editor.hpp"
#include "editor/EditorScriptSetup.hpp"
#include "editor/TryEditorContext.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputService.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TimeManager.hpp"
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

struct EditorClock{};

void EditorApp::Init() {
    graphics_context_ = std::make_unique<GraphicsContext>(1280, 720, "tryengine");

    engine_ = std::make_unique<Engine>();
    engine_->RegisterSystem<RenderSystem>(graphics_context_->GetDevice());
    engine_->RegisterSystem<RenderAdapter>();
    engine_->RegisterSystem<InputService>(this->input_state_);

    engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<AsyncFileManager>();
    engine_->RegisterSystem<ResourceManager>(engine_->Get<AsyncFileManager>(), engine_->Get<AssetRegistry>());
    auto& tm = engine_->RegisterSystem<TimeManager>();

    tm.RegisterClock<GameClock>();
    tm.RegisterClock<GameWorldClock, GameClock>();
    tm.RegisterClock<GameUIClock, GameClock>();

    tm.RegisterClock<EditorClock>();

    editor_ = std::make_unique<Editor>(*engine_, *graphics_context_);

    auto& script_system = engine_->RegisterSystem<ScriptSystem>(GetEditorScriptConfig(*engine_, *editor_));

    script_system.LoadMainScript("./editor/daslang/EntryPoint.das");

    const auto task =
        RunPollable(ThreadPool(), editor_->GetAssetSourceDatabase().InitEngineContentSync(engine_->Get<AsyncFileManager>()));

    while (!task.IsReady()) {
        engine_->Get<AsyncFileManager>().Pull();
        MainThread().Pull();
        engine_->Get<AsyncFileManager>().Submit();
        std::this_thread::yield();
    }

    engine_->Get<ScriptSystem>().InvokeFunctionFast("Start");

    RunAndForget(ThreadPool(), editor_->GetAssetSourceDatabase().AsyncLoadGameContent(engine_->Get<AsyncFileManager>()));
}

void EditorApp::Run() {
    editor_->running = true;
    editor_->state = PlayModeState::Edit;
    auto& render_system = engine_->Get<RenderSystem>();

    auto& tm = engine_->Get<TimeManager>();
    auto& script_system = engine_->Get<ScriptSystem>();

    while (editor_->running) {
        TRY_PROFILE_SCOPE("Main Loop Frame");
        {
            TRY_PROFILE_SCOPE("Render Less Frame");
            UpdateInput();

            tm.NewFrame();
            engine_->Get<AsyncFileManager>().Pull();
            MainThread().Pull();

            script_system.InvokeFunctionFast("EditorUpdate");

            if (editor_->state == PlayModeState::Play) {
                script_system.InvokeFunctionSafe("Update");
            }
            engine_->Get<AsyncFileManager>().Submit();

            editor_->GetGUI().RecordPanelsGpuCommands(editor_->state);
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
            auto camera_data = script_system.SimpleReturnUnsafe<CameraData*>("get_camera");
            render_system.RenderToTarget(cmd, *editor_->target, *(*camera_data));

            editor_->GetGUI().RenderToSwapchain(swapchain_texture, cmd);
        }
        Profiler::Instance().EndFrame(tm.Root().DeltaTime() * 1000.0f);
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
