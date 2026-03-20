#pragma once
#include <cstdint>

#include "EngineCommands.hpp"
#include "EngineTypes.hpp"
#include "SDL3/SDL_scancode.h"

namespace engine {
struct EngineSettings {
    bool isEditorMode = true;
    PresentMode presentMode = PresentMode::VSync;
    bool fullScreenEnable = false;
};

struct TimeState {
    uint64_t lastTime = 0;
    double deltaTime = 0.0;
    double fpsTimer = 0.0;
    int frameCount = 0;
    int currentFPS = 0;
};
} // namespace engine
