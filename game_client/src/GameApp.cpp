#include "GameApp.hpp"

#include "GameRender.hpp"
#include "GameScriptSetup.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TimeManager.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/OpaqueGeometryPass.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/platform/InputService.hpp"
#include "engine/resources/PakManager.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace trygame {

using namespace tryengine::core;
using namespace tryengine::graphics;
using namespace tryengine::resources;
using namespace tryengine::async;
using namespace tryengine::platform;

void GameApp::Init() {
    vulkan_device_ = std::make_unique<VulkanDevice>(1280, 720, "trygaem");
    frame_sync_ = std::make_unique<FrameSync>(*vulkan_device_);
    swapchain_ = std::make_unique<VulkanSwapchain>(*vulkan_device_, 1280, 720);

    engine_ = std::make_unique<Engine>();
    engine_->RegisterSystem<RenderGraph>(vulkan_device_->GetDevice());
    engine_->RegisterSystem<GameRender>(vulkan_device_, engine_->Get<RenderGraph>());
    engine_->RegisterSystem<InputService>();

    engine_->RegisterSystem<AsyncFileManager>();
    engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<PakManager>();
    engine_->RegisterSystem<ResourceManager>(engine_->Get<AsyncFileManager>(), engine_->Get<AssetRegistry>());
    auto& time_manager = engine_->RegisterSystem<TimeManager>();

    time_manager.RegisterClock<GameClock>();
    time_manager.RegisterClock<GameWorldClock, GameClock>();
    time_manager.RegisterClock<GameUIClock, GameClock>();

    engine_->RegisterSystem<ScriptSystem>(GetGameScriptConfig(*engine_));
    engine_->Get<ScriptSystem>().LoadMainScript("./game/assets/scripts/main.das");
    engine_->Get<ScriptSystem>().InvokeFunctionFast("Start");
}

void GameApp::Run() {
    running_ = true;
    auto& tm = engine_->Get<TimeManager>();
    auto& script_system = engine_->Get<ScriptSystem>();
    auto& render = engine_->Get<GameRender>();
    auto& input = engine_->Get<InputService>();
    auto& async_file_manager = engine_->Get<AsyncFileManager>();

    while (running_&& !input.IsQuitRequested()) {
        input.PollEvents();

        tm.NewFrame();
        async_file_manager.Pull();
        MainThread().Pull();

        script_system.InvokeFunctionSafe("Update");
        async_file_manager.Submit();

        render.Render(*engine_, *vulkan_device_);
    }
}

void GameApp::Shutdown() {}

}  // namespace trygame