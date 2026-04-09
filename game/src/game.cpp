#include <cstdio>

#include "game/GameAPI.hpp"

extern "C" {

  void UpdateGameSystems(tryengine::core::Engine* engine) {
      std::printf("UpdateGameSystems\n");
    // SDL_Log("Updating Game Systems...");
    // Вся логика обновления игровых систем теперь здесь
    // Например: UpdateTransformSystem(engine->GetRegistry());
  }

} // extern "C"