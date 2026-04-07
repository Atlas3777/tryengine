#pragma once
#include <SDL3/SDL.h>

#include "engine/core/InputState.hpp"

namespace editor {

class InputMapper {
public:
    static void ProcessEvent(const SDL_Event& event, engine::core::InputState& state);
};

} // namespace editor