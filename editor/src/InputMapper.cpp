#include "editor/InputMapper.hpp"

namespace editor {

void InputMapper::ProcessEvent(const SDL_Event& event, engine::core::InputState& state) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN: {
            auto key = static_cast<uint16_t>(event.key.scancode);
            if (key < static_cast<uint16_t>(engine::core::Key::Count)) {
                if (!event.key.repeat) {
                    state.justPressed[key] = true;
                }
                state.isDown[key] = true;
            }
            break;
        }

        case SDL_EVENT_KEY_UP: {
            auto key = static_cast<uint16_t>(event.key.scancode);
            if (key < static_cast<uint16_t>(engine::core::Key::Count)) {
                state.justReleased[key] = true;
                state.isDown[key] = false;
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            state.mouseX = event.motion.x;
            state.mouseY = event.motion.y;
            state.mouseDeltaX += event.motion.xrel;
            state.mouseDeltaY += event.motion.yrel;
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            int btnIdx = event.button.button - 1;
            if (btnIdx >= 0 && btnIdx < static_cast<int>(engine::core::Mouse::Count)) {
                if (!state.mouseButtons[btnIdx]) {
                    state.mouseJustPressed[btnIdx] = true;
                }
                state.mouseButtons[btnIdx] = true;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int btnIdx = event.button.button - 1;
            if (btnIdx >= 0 && btnIdx < static_cast<int>(engine::core::Mouse::Count)) {
                state.mouseJustReleased[btnIdx] = true;
                state.mouseButtons[btnIdx] = false;
            }
            break;
        }
        
        case SDL_EVENT_MOUSE_WHEEL: {
            // state.mouseScrollY = event.wheel.y; 
            break;
        }
    }
}

} // namespace editor