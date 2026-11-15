#include "editor/EditorApp.hpp"

#include "engine/resources/PendingLoadQueue.hpp"
#include "editor/AssetPipeline.hpp"
#include "editor/BuildSystem.hpp"
#include "editor/Editor.hpp"
#include "editor/EditorInputFilter.hpp"
#include "editor/EditorScriptSetup.hpp"
#include "editor/render/EditorRender.hpp"
#include "editor/shader/ShaderCompilerSystem.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/GameScriptSetup.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptBuildSystem.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TimeManager.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/loaders/MaterialLoader.hpp"
#include "engine/graphics/loaders/MeshLoader.hpp"
#include "engine/graphics/loaders/ShaderLoader.hpp"
#include "engine/graphics/loaders/TextureLoader.hpp"
#include "engine/platform/InputService.hpp"
#include "engine/resources/LooseFiles.hpp"
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

    auto& registry = engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<PendingLoadQueue>(registry);
    auto& async_file_manager = engine_->RegisterSystem<AsyncFileManager>();

    auto& rm = engine_->RegisterSystem<ResourceManager>(async_file_manager, registry);

    rm.RegisterType<Material>(MaterialLoader(*vulkan_device_, rm));
    rm.RegisterType<Mesh>(MeshLoader(*vulkan_device_));
    rm.RegisterType<Shader>(ShaderLoader(vulkan_device_->GetDevice()));
    rm.RegisterType<TextureSampler>(TextureLoader(*vulkan_device_));

    // Авторитетные материалы (game/data) не имеют .meta: регистрируем их по пути, чтобы
    // ссылки из сцен (id = PathGuid(путь)) находились и после перезапуска редактора.
    RegisterLooseDirectory(registry, "game/data", ".mat");

    engine_->RegisterSystem<BuildSystem>(engine_->Get<ResourceManager>(), async_file_manager);

    // Компилятор шейдеров живёт столько же, сколько редактор: global session Slang
    // создаётся один раз на контекст, а не на каждый импортируемый .slang.
    engine_->RegisterSystem<ShaderCompilerSystem>(ShaderCompilerConfig{
        .module_search_paths = {"engine_content/assets/shaders", "game/assets/shaders"},
        .context_count = 2,
    });

    engine_->RegisterSystem<InputService>().CreateFilter<EditorSDLEventFilter>();

    auto& time_manager = engine_->RegisterSystem<TimeManager>();
    time_manager.RegisterClock<EditorClock>();
    time_manager.RegisterClock<GameClock>();
    time_manager.RegisterClock<GameWorldClock, GameClock>();
    time_manager.RegisterClock<GameUIClock, GameClock>();

    editor_ = std::make_unique<Editor>(*engine_, *vulkan_device_);

    auto& script_system = engine_->RegisterSystem<ScriptSystem>(GetEditorScriptConfig(*engine_, *editor_));

    script_system.LoadMainScript("./editor/daslang/EntryPoint.das");

    engine_->RegisterSystem<ScriptBuildSystem>(GetGameScriptBuildConfig(*engine_));

    const auto task =
        RunPollable(ThreadPool(), editor_->GetAssetSourceDatabase().ImportEngineContent(async_file_manager));

    while (!task.IsReady()) {
        async_file_manager.Pull();
        MainThread().Pull();
        async_file_manager.Submit();
        std::this_thread::yield();
    }

    engine_->RegisterSystem<RenderGraph>(*vulkan_device_);
    engine_->RegisterSystem<EditorRender>(*engine_, *vulkan_device_, engine_->Get<RenderGraph>(), *swapchain_,
                                          *frame_sync_);

    engine_->Get<ScriptSystem>().InvokeFunctionFast("EngineStart");
    engine_->Get<ScriptSystem>().InvokeFunctionFast("EditorStart");

    RunAndForget(ThreadPool(), editor_->GetAssetSourceDatabase().ImportGameContent(engine_->Get<AsyncFileManager>()));
}

void EditorApp::Run() const {
    editor_->running = true;
    editor_->state = PlayModeState::Edit;

    auto& time_manager = engine_->Get<TimeManager>();
    auto& script_system = engine_->Get<ScriptSystem>();
    auto& render = engine_->Get<EditorRender>();
    auto& input = engine_->Get<InputService>();
    auto& async_file_manager = engine_->Get<AsyncFileManager>();

    while (editor_->running && !input.IsQuitRequested()) {
        TRY_PROFILE_SCOPE("Main Loop Frame");

        input.PollEvents();

        time_manager.NewFrame();
        async_file_manager.Pull();
        MainThread().Pull();

        script_system.InvokeFunctionFast("EngineUpdate");
        script_system.InvokeFunctionFast("EditorUpdate");

        engine_->Get<AsyncFileManager>().Submit();

        render.Render(*engine_, *vulkan_device_, editor_->state);
        // script_system.GetContext();
        Profiler::Instance().EndFrame(time_manager.Root().DeltaTime() * 1000.0f);
    }
}

void EditorApp::Shutdown() const {
    LogInfo("EditorApp shutting down...");
    vulkan_device_->WaitIdle();
}
}  // namespace tryeditor