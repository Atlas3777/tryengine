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
#include "engine/core/LogFileSink.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptBuildSystem.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/UiSystem.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/loaders/FontLoader.hpp"
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
    // Файл журнала открывается до всего остального: тогда в нём есть и ошибки инициализации
    // Vulkan. Кольцо окна Log регистрируется само при первом LogInfo; окно читает его.
    InstallLogFileSink();
    startup_begin_ = std::chrono::steady_clock::now();

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
    rm.RegisterType<Font>(FontLoader(*vulkan_device_));

    engine_->RegisterSystem<BuildSystem>(engine_->Get<ResourceManager>(), async_file_manager);

    // Компилятор шейдеров живёт столько же, сколько редактор: global session Slang
    // создаётся один раз на контекст, а не на каждый импортируемый .slang. Модули — обычные
    // исходники без .meta и артефактов; шейдеры подключают их через `import engine.*` /
    // `import game.*`, поэтому здесь только корни shader_modules.
    engine_->RegisterSystem<ShaderCompilerSystem>(ShaderCompilerConfig{
        .module_search_paths = {"engine_content/shader_modules", "game/shader_modules"},
        .context_count = 2,
    });

    engine_->RegisterSystem<InputService>().CreateFilter<EditorSDLEventFilter>();

    // Game UI: one font asset (guid from the .ttf .meta) and the UI material. Both load in the
    // background; UiPass draws nothing until they are ready.
    engine_->RegisterSystem<UiSystem>(UiConfig{}, *vulkan_device_, rm);

    editor_ = std::make_unique<Editor>(*engine_, *vulkan_device_);

    // Авторитетные корни (game/content, game/data) — одно сканирование диска: у их файлов
    // нет ни .meta, ни guid, ни артефактов, конвейер импорта им не нужен.
    editor_->GetAssetSourceDatabase().BuildAuthoritativeTrees();

    auto script_config = GetEditorScriptConfig(*engine_, *editor_);
    // JIT-компиляция скриптов — самая долгая часть загрузки: она идёт на пуле потоков,
    // пока главный поток импортирует движковый контент и рисует экран загрузки.
    script_config.post_background = [](eastl::function<void()> work) {
        ThreadPool().Post([work = eastl::move(work)]() mutable { work(); });
    };
    auto& script_system = engine_->RegisterSystem<ScriptSystem>(eastl::move(script_config));
    auto& script_reload = engine_->RegisterSystem<ScriptReload>(script_system);
    script_system.SetReloadExtension(&script_reload);

    // Возвращается сразу: контекст установится в Run/PollAsync, когда компиляция закончится.
    script_system.LoadMainScriptAsync("./editor/daslang/EntryPoint.das");

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

    RunAndForget(ThreadPool(), editor_->GetAssetSourceDatabase().ImportGameContent(engine_->Get<AsyncFileManager>()));
}

void EditorApp::Run() {
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

        // Переимпорт из панели Import Settings. Импорт уже записал артефакты (фоновая
        // задача), здесь main thread сбрасывает загруженные ресурсы: материал, который
        // ссылался на перезапечённую текстуру, обязан пересоздаться. Скрипты узнают о
        // событии по AssetPipeline::ReimportGeneration().
        if (auto reimported = editor_->GetAssetSourceDatabase().ApplyPendingTreeChanges(); !reimported.empty()) {
            TRY_PROFILE_SCOPE("Assets.Reimport");
            auto& resources = engine_->Get<ResourceManager>();
            for (const uint64_t guid : reimported) {
                resources.Invalidate<Shader>(guid);
                resources.Invalidate<Material>(guid);
                resources.Invalidate<Mesh>(guid);
                resources.Invalidate<TextureSampler>(guid);
                resources.Invalidate<Font>(guid);
            }
            resources.InvalidateAll<Material>();
        }

        // Единственное место, где устанавливается готовое поколение скриптов и вызываются
        // reload-хуки: до этого момента скриптовые функции не трогаем.
        {
            TRY_PROFILE_SCOPE("Script.Poll");
            script_system.PollAsync();
        }

        // Первая загрузка сломалась: скриптового UI ещё нет, поэтому F5 обрабатывает
        // кадровый цикл. Retry перечитывает файлы с диска и компилирует их тем же путём,
        // что и обычная первая загрузка; перезапуск процесса не нужен.
        if (script_system.IsFailed() && input.GetState().Pressed(Key::F5)) {
            TRY_PROFILE_SCOPE("Script.Retry");
            if (script_system.RetryMainScriptAsync())
                LogInfo(LogCategory::Script, "Повторная компиляция скриптов запрошена клавишей F5");
        }

        if (!scripts_started_ && script_system.IsReady()) {
            TRY_PROFILE_SCOPE("Script.Start");
            script_system.InvokeFunctionFast<"EngineStart">();
            script_system.InvokeFunctionFast<"EditorStart">();
            scripts_started_ = true;
            const double startup_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startup_begin_).count();
            LogInfo(LogCategory::Script, "Редактор готов за {:.1f} мс; скрипты готовы за {:.1f} мс", startup_ms,
                    script_system.LastCompileMs());
        }

        // Пока первая загрузка не закончилась (или провалилась), движок заперт: кадр
        // рисует C++ (экран загрузки/ошибки), а скриптовые точки входа не вызываются.
        // Во время фоновой перезагрузки старый контекст жив, но тоже не вызывается:
        // компилятор daScript не потокобезопасен, и кадр не должен в него залезать.
        if (scripts_started_ && script_system.IsReady()) {
            script_system.InvokeFunctionFast<"EngineUpdate">();
            script_system.InvokeFunctionFast<"EditorUpdate">();

            // Перезагрузка запрашивается из UI/хоткея во время EditorUpdate; подменять
            // контекст можно только когда в нём не выполняется ни одна функция скрипта.
            if (auto* reload = engine_->TryGet<ScriptReload>(); reload && reload->IsReloadRequested()) {
                TRY_PROFILE_SCOPE("Script.Reload");
                reload->ProcessRequested();
            }

            // Граница GC. Скрипты редактора живут на persistent_heap с options gc, но без
            // явного вызова сборщик не запускается, и каждый кадр панелей оставляет мусор.
            // DECS-компоненты держат строки и массивы в сырой памяти архетипов, поэтому
            // перед сборкой нужны gc_dummy-корни: их ставят/снимают скриптовые хуки.
            {
                TRY_PROFILE_SCOPE("Script.GC");
                script_system.InvokeFunctionFast<"EngineCollectGarbage">();
            }
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
