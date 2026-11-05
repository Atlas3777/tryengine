#include "GameApp.hpp"

#include "GameRender.hpp"
#include "engine/async/PollHandle.hpp"
#include "engine/core/GameScriptSetup.hpp"
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

    co_return  eastl::move(*r);
}

}  // namespace

void GameApp::Init() {
    vulkan_device_ = std::make_unique<VulkanDevice>(1280, 720, "trygame");
    frame_sync_ = std::make_unique<FrameSync>(*vulkan_device_);
    swapchain_ = std::make_unique<VulkanSwapchain>(*vulkan_device_, 1280, 720);

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
}

void GameApp::Run() {
    running_ = true;
    auto& tm = engine_->Get<TimeManager>();
    auto& script_system = engine_->Get<ScriptSystem>();
    auto& render = engine_->Get<GameRender>();
    auto& input = engine_->Get<InputService>();
    auto& async_file_manager = engine_->Get<AsyncFileManager>();

    while (running_ && !input.IsQuitRequested()) {
        input.PollEvents();

        tm.NewFrame();
        async_file_manager.Pull();
        MainThread().Pull();

        script_system.InvokeFunctionSafe("EngineUpdate");
        script_system.InvokeFunctionSafe("GameUpdate");

        async_file_manager.Submit();

        render.Render(*engine_, *vulkan_device_);
    }
}

void GameApp::Shutdown() const {
    vulkan_device_->WaitIdle();
}

}  // namespace trygame