#include "editor/EditorApp.hpp"

#include <cstdlib>
#include <string_view>

#include "editor/AssetPipeline.hpp"
#include "editor/BuildSystem.hpp"
#include "editor/Editor.hpp"
#include "editor/EditorInputFilter.hpp"
#include "editor/EditorScriptSetup.hpp"
#include "editor/ScriptReload.hpp"
#include "editor/render/EditorRender.hpp"
#include "editor/shader/ShaderCompilerSystem.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/GameScriptSetup.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptBuildSystem.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/loaders/MaterialLoader.hpp"
#include "engine/graphics/loaders/MeshLoader.hpp"
#include "engine/graphics/loaders/ShaderLoader.hpp"
#include "engine/graphics/loaders/TextureLoader.hpp"
#include "engine/physics/PhysicsWorld.hpp"
#include "engine/platform/InputService.hpp"
#include "engine/resources/PakManager.hpp"
#include "engine/resources/PendingLoadQueue.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryeditor {

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

using namespace tryengine::core;
using namespace tryengine::async;
using namespace tryengine::graphics;
using namespace tryengine::platform;
using namespace tryengine::resources;

void EditorApp::Init() {
    vulkan_device_ = std::make_unique<VulkanDevice>(1280, 720, "tryengine");
    frame_sync_ = std::make_unique<FrameSync>(*vulkan_device_);
    // vsync выключен: профилировать иначе бессмысленно, fps упирается в частоту монитора.
    swapchain_ = std::make_unique<VulkanSwapchain>(*vulkan_device_, 1280, 720, /*vsync=*/false);

    engine_ = std::make_unique<Engine>();
    engine_->RegisterSystem<tryengine::physics::PhysicsWorld>();

    auto& registry = engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<PendingLoadQueue>(registry);
    auto& async_file_manager = engine_->RegisterSystem<AsyncFileManager>();

    auto& rm = engine_->RegisterSystem<ResourceManager>(async_file_manager, registry);

    rm.RegisterType<Material>(MaterialLoader(*vulkan_device_, rm));
    rm.RegisterType<Mesh>(MeshLoader(*vulkan_device_));
    rm.RegisterType<Shader>(ShaderLoader(vulkan_device_->GetDevice()));
    rm.RegisterType<TextureSampler>(TextureLoader(*vulkan_device_));

    engine_->RegisterSystem<BuildSystem>(engine_->Get<ResourceManager>(), async_file_manager);

    // Компилятор шейдеров живёт столько же, сколько редактор: global session Slang
    // создаётся один раз на контекст, а не на каждый импортируемый .slang.
    engine_->RegisterSystem<ShaderCompilerSystem>(ShaderCompilerConfig{
        .module_search_paths = {"engine_content/assets/shaders", "game/assets/shaders"},
        .context_count = 2,
    });

    engine_->RegisterSystem<InputService>().CreateFilter<EditorSDLEventFilter>();

    editor_ = std::make_unique<Editor>(*engine_, *vulkan_device_);

    // Авторитетные корни (game/content, game/data) — одно сканирование диска: у их файлов
    // нет ни .meta, ни guid, ни артефактов, конвейер импорта им не нужен.
    editor_->GetAssetSourceDatabase().BuildAuthoritativeTrees();

    auto& script_system = engine_->RegisterSystem<ScriptSystem>(GetEditorScriptConfig(*engine_, *editor_));
    auto& script_reload = engine_->RegisterSystem<ScriptReload>(script_system);
    script_system.SetReloadExtension(&script_reload);

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

    auto& script_system = engine_->Get<ScriptSystem>();
    auto& render = engine_->Get<EditorRender>();
    auto& input = engine_->Get<InputService>();
    auto& async_file_manager = engine_->Get<AsyncFileManager>();

    // В игре каптион включается сам, здесь — только по TRY_PROFILE_CAPTURE=1: редактору
    // обычно хватает панели, а прогону без человека (и проверке профайлера) нужен файл.
    const char* capture = std::getenv("TRY_PROFILE_CAPTURE");
    if (capture != nullptr && std::string_view(capture) != "0")
        Profiler::Instance().StartCapture();

    while (editor_->running && !input.IsQuitRequested()) {
        // Корень кадра ставит сам профайлер: его длительность и есть время кадра, поэтому
        // полоса "EditorApp::Run" всегда тянется от начала кадра до его конца. Всё, что
        // делается между итерациями, тоже попадает внутрь корня, а не в пустоту.
        TRY_PROFILE_BEGIN_FRAME("EditorApp::Run");

        {
            TRY_PROFILE_SCOPE("Input.PollEvents");
            input.PollEvents();
        }
        {
            TRY_PROFILE_SCOPE("Async.Pull");
            async_file_manager.Pull();
            MainThread().Pull();
        }

        script_system.InvokeFunctionFast("EngineUpdate");
        script_system.InvokeFunctionFast("EditorUpdate");

        // Перезагрузка запрашивается из UI/хоткея во время EditorUpdate; подменять контекст
        // можно только когда в нём не выполняется ни одна функция скрипта, то есть здесь.
        if (auto* reload = engine_->TryGet<ScriptReload>(); reload && reload->IsReloadRequested()) {
            TRY_PROFILE_SCOPE("Script.Reload");
            reload->ProcessRequested();
        }

        {
            TRY_PROFILE_SCOPE("Async.Submit");
            engine_->Get<AsyncFileManager>().Submit();
        }
        {
            TRY_PROFILE_SCOPE("Render");
            render.Render(*engine_, *vulkan_device_, editor_->state);
        }

        TRY_PROFILE_END_FRAME();
    }
}

void EditorApp::Shutdown() const {
    LogInfo("EditorApp shutting down...");
    Profiler::Instance().StopCapture();
    vulkan_device_->WaitIdle();
}
}  // namespace tryeditor
