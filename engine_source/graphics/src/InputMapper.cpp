#include "engine/graphics/InputMapper.hpp"

namespace tryeditor {

void InputMapper::ProcessEvent(const SDL_Event& event, tryengine::core::InputState& state) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN: {
            auto key = static_cast<uint16_t>(event.key.scancode);
            if (key < static_cast<uint16_t>(tryengine::core::Key::Count)) {
                if (!event.key.repeat) {
                    state.just_pressed[key] = true;
                }
                state.is_down[key] = true;
            }
            break;
        }

        case SDL_EVENT_KEY_UP: {
            auto key = static_cast<uint16_t>(event.key.scancode);
            if (key < static_cast<uint16_t>(tryengine::core::Key::Count)) {
                state.just_released[key] = true;
                state.is_down[key] = false;
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            state.mouse_x = event.motion.x;
            state.mouse_y = event.motion.y;
            state.mouse_delta_x += event.motion.xrel;
            state.mouse_delta_y += event.motion.yrel;
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            int btnIdx = event.button.button - 1;
            if (btnIdx >= 0 && btnIdx < static_cast<int>(tryengine::core::Mouse::Count)) {
                if (!state.mouse_buttons[btnIdx]) {
                    state.mouse_just_pressed[btnIdx] = true;
                }
                state.mouse_buttons[btnIdx] = true;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int btnIdx = event.button.button - 1;
            if (btnIdx >= 0 && btnIdx < static_cast<int>(tryengine::core::Mouse::Count)) {
                state.mouse_just_released[btnIdx] = true;
                state.mouse_buttons[btnIdx] = false;
            }
            break;
        }
        
        case SDL_EVENT_MOUSE_WHEEL: {
            // state.mouseScrollY = event.wheel.y; 
            break;
        }
    }
}

} // namespace tryeditor