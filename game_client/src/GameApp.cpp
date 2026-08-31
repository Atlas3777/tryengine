#include "GameApp.hpp"

#include "GameScriptSetup.hpp"
#include "Render.hpp"
#include "engine/core/InputService.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TimeManager.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/InputMapper.hpp"
#include "engine/graphics/RenderAdapter.hpp"
#include "engine/graphics/RenderSystem.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace trygame {

using namespace tryengine::core;
using namespace tryengine::graphics;
using namespace tryengine::resources;
using namespace tryengine::async;

void GameApp::Init() {
    graphics_context_ = std::make_unique<GraphicsContext>(1280, 720, "trygame");

    target =
        std::make_unique<RenderTarget>(graphics_context_->GetDevice(), 1280, 720, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

    engine_ = std::make_unique<Engine>();
    engine_->RegisterSystem<RenderSystem>(graphics_context_->GetDevice());
    engine_->RegisterSystem<RenderAdapter>();
    engine_->RegisterSystem<InputService>(this->input_state_);

    engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<ResourceManager>(async_file_manager_, engine_->Get<AssetRegistry>());
    auto& tm = engine_->RegisterSystem<TimeManager>();

    tm.RegisterClock<GameClock>();
    tm.RegisterClock<GameWorldClock, GameClock>();
    tm.RegisterClock<GameUIClock, GameClock>();

    engine_->RegisterSystem<ScriptSystem>(GetGameScriptConfig(*engine_));
    engine_->Get<ScriptSystem>().LoadMainScript("./game/assets/scripts/main.das");
    engine_->Get<ScriptSystem>().InvokeFunctionFast("Start");
}

void GameApp::Run() {
    running_ = true;
    auto& render_system = engine_->Get<RenderSystem>();
    auto& tm = engine_->Get<TimeManager>();
    auto& script_system = engine_->Get<ScriptSystem>();

    while (running_) {
        UpdateInput();

        tm.NewFrame();
        async_file_manager_.Pull();
        MainThread().Pull();

        script_system.InvokeFunctionSafe("Update");
        async_file_manager_.Submit();
        const auto cmd = SDL_AcquireGPUCommandBuffer(graphics_context_->GetDevice());

        SDL_GPUTexture* swapchain_texture = nullptr;
        uint32_t w, h;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, graphics_context_->GetWindow(), &swapchain_texture, &w, &h)) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }

        engine_->Get<RenderAdapter>().CollectDrawable(*engine_, render_system);
        auto camera_data = script_system.SimpleReturnUnsafe<CameraData*>("get_camera");
        render_system.RenderToTarget(cmd, *target, *(*camera_data));

        RenderToSwapchain(cmd, target->GetColor(), target->GetWidth(), target->GetHeight(), swapchain_texture, w, h);
    }
}

void GameApp::UpdateInput() {
    input_state_.ResetFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running_ = false;
            continue;
        }

        tryeditor::InputMapper::ProcessEvent(event, input_state_);
    }
}

void GameApp::Shutdown() {}

}  // namespace trygame