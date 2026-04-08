#pragma once
#include <SDL3/SDL.h>

#include "engine/core/InputState.hpp"

namespace tryeditor {

class InputMapper {
public:
    static void ProcessEvent(const SDL_Event& event, tryengine::core::InputState& state);
};

} // namespace tryeditor