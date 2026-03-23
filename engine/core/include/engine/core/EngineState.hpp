#pragma once
#include <cstdint>

#include "CoreTypes.hpp"
#include "SDL3/SDL_scancode.h"
#include "engine/core/EngineCommands.hpp"

namespace engine::core {
struct EngineSettings {
    PresentMode presentMode = PresentMode::VSync;
    bool fullScreenEnable = false;
};
} // namespace engine
