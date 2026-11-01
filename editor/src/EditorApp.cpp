#include "editor/EditorApp.hpp"

#include "../include/editor/render/EditorRender.hpp"
#include "editor/AssetPipeline.hpp"
#include "editor/BuildSystem.hpp"
#include "editor/Editor.hpp"
#include "editor/EditorInputFilter.hpp"
#include "editor/EditorScriptSetup.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TimeManager.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/loaders/MaterialLoader.hpp"
#include "engine/graphics/loaders/MeshLoader.hpp"
#include "engine/graphics/loaders/ShaderLoader.hpp"
#include "engine/graphics/loaders/TextureLoader.hpp"
#include "engine/platform/InputService.hpp"
#include "engine/resources/PakManager.hpp"
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

    engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<AsyncFileManager>();

    auto& rm = engine_->RegisterSystem<ResourceManager>(engine_->Get<AsyncFileManager>(), engine_->Get<AssetRegistry>());

    rm.RegisterType<Material>(MaterialLoader(*vulkan_device_, rm));
    rm.RegisterType<Mesh>(MeshLoader(*vulkan_device_));
    rm.RegisterType<Shader>(ShaderLoader(vulkan_device_->GetDevice()));
    rm.RegisterType<TextureSampler>(TextureLoader(*vulkan_device_));

    engine_->RegisterSystem<BuildSystem>(engine_->Get<ResourceManager>(), engine_->Get<AsyncFileManager>());

    engine_->RegisterSystem<InputService>().CreateFilter<EditorSDLEventFilter>();

    auto& time_manager = engine_->RegisterSystem<TimeManager>();
    time_manager.RegisterClock<EditorClock>();
    time_manager.RegisterClock<GameClock>();
    time_manager.RegisterClock<GameWorldClock, GameClock>();
    time_manager.RegisterClock<GameUIClock, GameClock>();

    editor_ = std::make_unique<Editor>(*engine_, *vulkan_device_);

    auto& script_system = engine_->RegisterSystem<ScriptSystem>(GetEditorScriptConfig(*engine_, *editor_));

    script_system.LoadMainScript("./editor/daslang/EntryPoint.das");

    const auto task = RunPollable(
        ThreadPool(), editor_->GetAssetSourceDatabase().ImportEngineContent(engine_->Get<AsyncFileManager>()));

    while (!task.IsReady()) {
        engine_->Get<AsyncFileManager>().Pull();
        MainThread().Pull();
        engine_->Get<AsyncFileManager>().Submit();
        std::this_thread::yield();
    }

    engine_->RegisterSystem<RenderGraph>(*vulkan_device_);
    engine_->RegisterSystem<EditorRender>(*engine_, *vulkan_device_, engine_->Get<RenderGraph>(), *swapchain_,
                                          *frame_sync_);

    engine_->Get<ScriptSystem>().InvokeFunctionFast("EngineStart");
    engine_->Get<ScriptSystem>().InvokeFunctionFast("GameStart");

    RunAndForget(ThreadPool(), editor_->GetAssetSourceDatabase().ImportGameContent(engine_->Get<AsyncFileManager>()));
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

        script_system.InvokeFunctionFast("EngineUpdate");
        script_system.InvokeFunctionFast("EditorUpdate");

        if (editor_->state == PlayModeState::Play)
            script_system.InvokeFunctionSafe("GameUpdate");

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
