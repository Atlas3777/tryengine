#include "engine/core/Engine.hpp"
#include "engine/core/GameAPI.hpp"
#include "game/GameMain.hpp"

extern "C" {
void Game_OnInit(tryengine::core::Engine& engine) {
    game::GameMain::InitializeSystems(engine);
}

void Game_OnUpdate(tryengine::core::Engine& engine, float deltaTime) {
    game::GameMain::UpdateAllSystems(engine, deltaTime);
}

void Game_OnShutdown(tryengine::core::Engine& engine) {
    game::GameMain::Cleanup(engine);
}
}