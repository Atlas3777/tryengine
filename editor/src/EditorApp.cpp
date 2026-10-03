#include "editor/EditorApp.hpp"

#include "editor/AssetPipeline.hpp"
#include "editor/Editor.hpp"
#include "editor/EditorInputFilter.hpp"
#include "editor/EditorRender.hpp"
#include "editor/EditorScriptSetup.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TimeManager.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/platform/InputService.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryeditor {

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

using namespace tryengine::core;
using namespace tryengine::async;
using namespace tryengine::graphics;
using namespace tryengine::platform;
using namespace tryengine::resources;

struct EditorClock {};

void EditorApp::Init() {
    vulkan_device_ = std::make_unique<VulkanDevice>(1280, 720, "tryengine");
    frame_sync_ = std::make_unique<FrameSync>(*vulkan_device_);
    swapchain_ = std::make_unique<VulkanSwapchain>(*vulkan_device_, 1280, 720);

    engine_ = std::make_unique<Engine>();
    engine_->RegisterSystem<RenderGraph>(*vulkan_device_);
    engine_->RegisterSystem<EditorRender>(*vulkan_device_, engine_->Get<RenderGraph>(), *swapchain_, *frame_sync_);
    engine_->RegisterSystem<InputService>();
    engine_->Get<InputService>().CreateFilter<EditorSDLEventFilter>();

    engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<AsyncFileManager>();
    engine_->RegisterSystem<ResourceManager>(engine_->Get<AsyncFileManager>(), engine_->Get<AssetRegistry>());
    auto& time_manager = engine_->RegisterSystem<TimeManager>();

    time_manager.RegisterClock<GameClock>();
    time_manager.RegisterClock<GameWorldClock, GameClock>();
    time_manager.RegisterClock<GameUIClock, GameClock>();

    time_manager.RegisterClock<EditorClock>();

    editor_ = std::make_unique<Editor>(*engine_, *vulkan_device_);

    auto& script_system = engine_->RegisterSystem<ScriptSystem>(GetEditorScriptConfig(*engine_, *editor_));

    script_system.LoadMainScript("./editor/daslang/EntryPoint.das");

    const auto task = RunPollable(
        ThreadPool(), editor_->GetAssetSourceDatabase().InitEngineContentSync(engine_->Get<AsyncFileManager>()));

    while (!task.IsReady()) {
        engine_->Get<AsyncFileManager>().Pull();
        MainThread().Pull();
        engine_->Get<AsyncFileManager>().Submit();
        std::this_thread::yield();
    }

    auto deb_shader = engine_->Get<ResourceManager>().Get<Shader>(10245112668937909950ULL);
    if (!deb_shader.has_value())
        TRY_ASSERT(deb_shader.has_value(), "Нет шейдера debug");
    engine_->Get<EditorRender>().SetDebugShader(*deb_shader);

    auto pick_shader = engine_->Get<ResourceManager>().Get<Shader>(4250482409988402633ULL);
    if (!pick_shader.has_value())
        TRY_ASSERT(pick_shader.has_value(), "Нет шейдера picking");
    engine_->Get<EditorRender>().SetPickShader(*pick_shader);


    engine_->Get<ScriptSystem>().InvokeFunctionFast("Start");

    RunAndForget(ThreadPool(),
                 editor_->GetAssetSourceDatabase().AsyncLoadGameContent(engine_->Get<AsyncFileManager>()));
}

void EditorApp::Run() const {
    editor_->running = true;
    editor_->state = PlayModeState::Edit;

    auto& time_manager = engine_->Get<TimeManager>();
    auto& script_system = engine_->Get<ScriptSystem>();
    auto& render = engine_->Get<EditorRender>();
    auto& input = engine_->Get<InputService>();

    while (editor_->running && !input.IsQuitRequested()) {
        TRY_PROFILE_SCOPE("Main Loop Frame");

        input.PollEvents();

        time_manager.NewFrame();
        engine_->Get<AsyncFileManager>().Pull();
        MainThread().Pull();

        script_system.InvokeFunctionFast("EditorUpdate");

        if (editor_->state == PlayModeState::Play)
            script_system.InvokeFunctionSafe("Update");

        engine_->Get<AsyncFileManager>().Submit();

        render.Render(*engine_, *vulkan_device_, editor_->state);
        Profiler::Instance().EndFrame(time_manager.Root().DeltaTime() * 1000.0f);
    }
}

void EditorApp::Shutdown() const {
    LogInfo("EditorApp shutting down...");
    vulkan_device_->WaitIdle();
}
}  // namespace tryeditor
