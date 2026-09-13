#include "editor/EditorApp.hpp"

#include <imgui_impl_sdl3.h>

#include "editor/AssetPipeline.hpp"
#include "editor/CreateFile.hpp"
#include "editor/Editor.hpp"
#include "editor/EditorRender.hpp"
#include "editor/EditorScriptSetup.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputService.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TimeManager.hpp"
#include "engine/graphics/InputMapper.hpp"
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
    engine_->RegisterSystem<RenderGraph>(graphics_context_->GetDevice());
    engine_->RegisterSystem<EditorRender>(*graphics_context_, engine_->Get<RenderGraph>());
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

    auto shader_r = engine_->Get<ResourceManager>().Get<Shader>(10245112668937909950);
    if (shader_r.has_value()) {
        engine_->Get<EditorRender>().SetDebugShader(*shader_r);
    }
    else {
        LogError("Не нашли кеш..?");
    }

    engine_->Get<ScriptSystem>().InvokeFunctionFast("Start");

    RunAndForget(ThreadPool(), editor_->GetAssetSourceDatabase().AsyncLoadGameContent(engine_->Get<AsyncFileManager>()));
}

void EditorApp::Run() {
    editor_->running = true;
    editor_->state = PlayModeState::Edit;

    auto& tm = engine_->Get<TimeManager>();
    auto& script_system = engine_->Get<ScriptSystem>();
    auto& render = engine_->Get<EditorRender>();

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

        }

        render.Render(*engine_, *graphics_context_, editor_->state);
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
