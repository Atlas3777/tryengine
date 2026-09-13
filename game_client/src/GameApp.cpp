#include "GameApp.hpp"

#include "GameRender.hpp"
#include "GameScriptSetup.hpp"
#include "engine/core/InputService.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TimeManager.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/InputMapper.hpp"
#include "engine/graphics/OpaqueGeometryPass.hpp"
#include "engine/resources/PakManager.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace trygame {

using namespace tryengine::core;
using namespace tryengine::graphics;
using namespace tryengine::resources;
using namespace tryengine::async;

void GameApp::Init() {
    graphics_context_ = std::make_unique<GraphicsContext>(1280, 720, "trygame");

    engine_ = std::make_unique<Engine>();
    engine_->RegisterSystem<RenderGraph>(graphics_context_->GetDevice());
    engine_->RegisterSystem<GameRender>(graphics_context_, engine_->Get<RenderGraph>());
    engine_->RegisterSystem<InputService>(this->input_state_);

    engine_->RegisterSystem<AssetRegistry>();
    engine_->RegisterSystem<PakManager>();
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
    auto& tm = engine_->Get<TimeManager>();
    auto& script_system = engine_->Get<ScriptSystem>();
    auto& render = engine_->Get<GameRender>();

    while (running_) {
        UpdateInput();

        tm.NewFrame();
        async_file_manager_.Pull();
        MainThread().Pull();

        script_system.InvokeFunctionSafe("Update");
        async_file_manager_.Submit();

        render.Render(*engine_, *graphics_context_);
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