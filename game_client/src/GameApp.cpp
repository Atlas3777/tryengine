#include "GameApp.hpp"

#include <cstdlib>
#include <string_view>

#include "GameRender.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/GameScriptSetup.hpp"
#include "engine/core/LogFileSink.hpp"
#include "engine/core/Profiler.hpp"
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
#include "engine/resources/ReadFullFile.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace trygame {

using namespace tryengine::core;
using namespace tryengine::graphics;
using namespace tryengine::resources;
using namespace tryengine::async;
using namespace tryengine::platform;

namespace {

Task<eastl::vector<uint8_t>> LoadScriptPak(AsyncFileManager& fm) {
    tryengine::Result<eastl::vector<uint8_t>> r = co_await ReadFullFileNonCopy(fm, "script_pak");
    if (!r.has_value())
        co_return LogAndMakeError("Не удалось прочитать script_pak");

    co_return eastl::move(*r);
}

}  // namespace

void GameApp::Init() {
    // У игры нет окна журнала: файл — единственное место, где её лог остаётся после
    // закрытия. Пишут и C++, и скрипты через один Logger.
    InstallLogFileSink();
    startup_begin_ = std::chrono::steady_clock::now();

    vulkan_device_ = std::make_unique<VulkanDevice>(1280, 720, "trygame");
    frame_sync_ = std::make_unique<FrameSync>(*vulkan_device_);
    swapchain_ = std::make_unique<VulkanSwapchain>(*vulkan_device_, 1280, 720, /*vsync=*/false);

    engine_ = std::make_unique<Engine>();
    engine_->RegisterSystem<tryengine::physics::PhysicsWorld>();
    engine_->RegisterSystem<RenderGraph>(*vulkan_device_);
    engine_->RegisterSystem<GameRender>(*vulkan_device_, engine_->Get<RenderGraph>(), *swapchain_, *frame_sync_);
    engine_->RegisterSystem<InputService>();

    auto& fm = engine_->RegisterSystem<AsyncFileManager>();
    auto& registry = engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<PendingLoadQueue>(registry);

    auto& pak_manager = engine_->RegisterSystem<PakManager>(fm, registry);

    auto& rm = engine_->RegisterSystem<ResourceManager>(fm, registry);

    rm.RegisterType<Material>(MaterialLoader(*vulkan_device_, rm));
    rm.RegisterType<Mesh>(MeshLoader(*vulkan_device_));
    rm.RegisterType<Shader>(ShaderLoader(vulkan_device_->GetDevice()));
    rm.RegisterType<TextureSampler>(TextureLoader(*vulkan_device_));
    rm.RegisterType<Font>(FontLoader(*vulkan_device_));

    // Game UI: the loader brings the font and the UI material from `ui.bundle`, and the game
    // hands them over with UiSetAssets. UiPass draws nothing until that happened.
    engine_->RegisterSystem<UiSystem>(UiConfig{}, *vulkan_device_, rm);

    // Паки монтируются до старта скриптов: тексты читаются через тот же API, что и в
    // редакторе. GameStart собирает LoaderRequest и запускает задачу Loader::Load.
    auto paks_task = RunPollable(ThreadPool(), pak_manager.LoadAllAsync());
    while (!paks_task.IsReady()) {
        fm.Pull();
        MainThread().Pull();
        fm.Submit();
        std::this_thread::yield();
    }
    TRY_ASSERT(paks_task.GetResult().has_value(), "Не удалось смонтировать content паки");

    auto pak_task = RunPollable(ThreadPool(), LoadScriptPak(fm));
    while (!pak_task.IsReady()) {
        fm.Pull();
        MainThread().Pull();
        fm.Submit();
        std::this_thread::yield();
    };
    TRY_ASSERT(pak_task.GetResult().has_value(), "Не удалось загрузить script_pak");

    auto script_config = GetGameScriptConfig(*engine_, eastl::move(*pak_task.GetResult()));
    // JIT-компиляция идёт на пуле потоков: главный поток продолжает кадры, а
    // EngineStart/GameStart вызываются первым кадром после установки контекста. Fallback-
    // оверлей UiPass включается только после boot-запроса ui.bundle, а он идёт уже после
    // компиляции: на время JIT окно показывает цвет очистки.
    script_config.post_background = [](eastl::function<void()> work) {
        ThreadPool().Post([work = eastl::move(work)]() mutable { work(); });
    };
    engine_->RegisterSystem<ScriptSystem>(eastl::move(script_config));

    // Прогрева контента здесь нет: GameStart запускает задачу Loader::Load, а она
    // читает тексты и запрашивает меши/материалы; GameUpdate не знает про загрузку.
    engine_->Get<ScriptSystem>().LoadMainScriptAsync("./game/scripts/Game.das");

    // У игры нет ImGui: смотреть на её кадры можно только по файлам в logs/. Каптион пишет
    // компактный поток кадров и строку статуса раз в секунду прямо из кадра, так что
    // `tail -f logs/profiler-*.txt` показывает fps и главных едоков времени на ходу.
    // TRY_PROFILE_CAPTURE=0 выключает запись.
    const char* capture = std::getenv("TRY_PROFILE_CAPTURE");
    if (capture == nullptr || std::string_view(capture) != "0")
        Profiler::Instance().StartCapture();
}

void GameApp::Run() {
    running_ = true;
    auto& script_system = engine_->Get<ScriptSystem>();
    auto& render = engine_->Get<GameRender>();
    auto& input = engine_->Get<InputService>();
    auto& async_file_manager = engine_->Get<AsyncFileManager>();

    while (running_ && !input.IsQuitRequested()) {
        // Корень кадра ставит профайлер: время кадра — это длительность корня, поэтому его
        // полоса на таймлайне всегда в точности равна кадру.
        TRY_PROFILE_BEGIN_FRAME("GameApp::Run");

        {
            TRY_PROFILE_SCOPE("Input.PollEvents");
            input.PollEvents();
        }
        {
            TRY_PROFILE_SCOPE("Async.Pull");
            async_file_manager.Pull();
            MainThread().Pull();
        }

        // Готовое поколение устанавливается только здесь. Пока компиляция идёт или
        // провалилась, скриптовые точки входа не вызываются: кадр рисует C++-экран
        // загрузки/ошибки, а движок остаётся запертым.
        {
            TRY_PROFILE_SCOPE("Script.Poll");
            script_system.PollAsync();
        }

        if (!scripts_started_ && script_system.IsReady()) {
            TRY_PROFILE_SCOPE("Script.Start");
            script_system.InvokeFunctionFast<"EngineStart">();
            script_system.InvokeFunctionFast<"GameStart">();
            scripts_started_ = true;
            const double startup_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startup_begin_).count();
            LogInfo(LogCategory::Script, "Игра готова за {:.1f} мс; скрипты готовы за {:.1f} мс", startup_ms,
                    script_system.LastCompileMs());
        }

        if (scripts_started_ && script_system.IsReady()) {
            script_system.InvokeFunctionSafe<"EngineUpdate">();
            // UI объявляется каждый кадр на своём клоке и не зависит от gameplay: пауза игрового
            // мира не должна замораживать экраны. Поэтому это отдельная точка входа, а не часть
            // GameUpdate.
            script_system.InvokeFunctionSafe<"GameUiFrame">();
            script_system.InvokeFunctionSafe<"GameUpdate">();
        }

        {
            TRY_PROFILE_SCOPE("Async.Submit");
            async_file_manager.Submit();
        }
        {
            TRY_PROFILE_SCOPE("Render");
            render.Render(*engine_, *vulkan_device_);
        }

        TRY_PROFILE_END_FRAME();
    }
}

void GameApp::Shutdown() const {
    Profiler::Instance().StopCapture();
    vulkan_device_->WaitIdle();
}

}  // namespace trygame