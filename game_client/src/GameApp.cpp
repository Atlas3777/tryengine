#include "GameApp.hpp"

#include <cstdlib>
#include <string_view>

#include "GameRender.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/GameScriptSetup.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TimeManager.hpp"
#include "engine/graphics/OpaquePass.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/loaders/MaterialLoader.hpp"
#include "engine/graphics/loaders/MeshLoader.hpp"
#include "engine/graphics/loaders/ShaderLoader.hpp"
#include "engine/graphics/loaders/TextureLoader.hpp"
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
    vulkan_device_ = std::make_unique<VulkanDevice>(1280, 720, "trygame");
    frame_sync_ = std::make_unique<FrameSync>(*vulkan_device_);
    swapchain_ = std::make_unique<VulkanSwapchain>(*vulkan_device_, 1280, 720, /*vsync=*/false);

    engine_ = std::make_unique<Engine>();
    engine_->RegisterSystem<RenderGraph>(*vulkan_device_);
    engine_->RegisterSystem<GameRender>(*vulkan_device_, engine_->Get<RenderGraph>(), *swapchain_, *frame_sync_);
    engine_->RegisterSystem<InputService>();

    auto& fm = engine_->RegisterSystem<AsyncFileManager>();
    auto& registry = engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<PendingLoadQueue>(registry);

    engine_->RegisterSystem<PakManager>(fm, registry).StartLoadAll();

    auto& rm = engine_->RegisterSystem<ResourceManager>(fm, registry);

    rm.RegisterType<Material>(MaterialLoader(*vulkan_device_, rm));
    rm.RegisterType<Mesh>(MeshLoader(*vulkan_device_));
    rm.RegisterType<Shader>(ShaderLoader(vulkan_device_->GetDevice()));
    rm.RegisterType<TextureSampler>(TextureLoader(*vulkan_device_));

    auto& time_manager = engine_->RegisterSystem<TimeManager>();

    time_manager.RegisterClock<GameClock>();
    time_manager.RegisterClock<GameWorldClock, GameClock>();
    time_manager.RegisterClock<GameUIClock, GameClock>();

    auto pak_task = RunPollable(ThreadPool(), LoadScriptPak(fm));
    while (!pak_task.IsReady()) {
        fm.Pull();
        MainThread().Pull();
        fm.Submit();
        std::this_thread::yield();
    };
    TRY_ASSERT(pak_task.GetResult().has_value(), "Не удалось загрузить script_pak");

    engine_->RegisterSystem<ScriptSystem>(GetGameScriptConfig(*engine_, eastl::move(*pak_task.GetResult())));
    engine_->Get<ScriptSystem>().LoadMainScript("./game/scripts/Game.das");
    engine_->Get<ScriptSystem>().InvokeFunctionFast("EngineStart");
    engine_->Get<ScriptSystem>().InvokeFunctionFast("GameStart");

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
    auto& tm = engine_->Get<TimeManager>();
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
            TRY_PROFILE_SCOPE("Time.NewFrame");
            tm.NewFrame();
        }
        {
            TRY_PROFILE_SCOPE("Async.Pull");
            async_file_manager.Pull();
            MainThread().Pull();
        }

        script_system.InvokeFunctionSafe("EngineUpdate");
        script_system.InvokeFunctionSafe("GameUpdate");
        // The authored Transform is local; the renderer reads WorldTransform.
        script_system.InvokeFunctionSafe("UpdateWorldTransforms");

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