#include "../../engine/core/include/engine/core/Engine.hpp"
#include "game/GameAPI.hpp"

extern "C" {

  void UpdateGameSystems(engine::Engine* engine) {
    SDL_Log("Updating Game Systems...");
    // Вся логика обновления игровых систем теперь здесь
    // Например: UpdateTransformSystem(engine->GetRegistry());
  }

  engine::RenderTarget* Rendering(engine::Engine* engine) {
    // Вся логика игрового рендера
    return nullptr; // Верни свой актуальный RenderTarget
  }

} // extern "C"