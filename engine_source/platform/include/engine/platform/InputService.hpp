#pragma once

#include <EASTL/vector.h>
#include <SDL3/SDL.h>
#include <memory>

#include "ISDLEventFilter.hpp"
#include "InputState.hpp"

namespace tryengine::platform {

inline void ProcessEvent(const SDL_Event& event, InputState& state) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN: {
            auto key = static_cast<uint16_t>(event.key.scancode);
            if (key < static_cast<uint16_t>(Key::Count)) {
                if (!event.key.repeat) {
                    state.just_pressed[key] = true;
                }
                state.is_down[key] = true;
            }
            break;
        }

        case SDL_EVENT_KEY_UP: {
            auto key = static_cast<uint16_t>(event.key.scancode);
            if (key < static_cast<uint16_t>(Key::Count)) {
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
            if (btnIdx >= 0 && btnIdx < static_cast<int>(Mouse::Count)) {
                if (!state.mouse_buttons[btnIdx]) {
                    state.mouse_just_pressed[btnIdx] = true;
                }
                state.mouse_buttons[btnIdx] = true;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int btnIdx = event.button.button - 1;
            if (btnIdx >= 0 && btnIdx < static_cast<int>(Mouse::Count)) {
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

class InputService {
public:
    template <typename T, typename... Args>
    T& CreateFilter(Args&&... args) {
        auto filter = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *filter;
        filters_.push_back(std::move(filter));
        return ref;
    }

    void PollEvents() {
        input_state_.ResetFrame();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            bool handled = false;
            for (auto& filter : filters_) {
                if (filter->OnSDLEvent(event)) {
                    handled = true;
                    break;  // Фильтр (например ImGui) забрал событие себе
                }
            }

            if (handled) {
                continue;
            }

            if (event.type == SDL_EVENT_QUIT) {
                quit_requested_ = true;
                continue;
            }

            ProcessEvent(event, input_state_);
        }
    }

    [[nodiscard]] bool IsQuitRequested() const noexcept { return quit_requested_; }
    [[nodiscard]] InputState& GetState() noexcept { return input_state_; }

private:
    InputState input_state_;
    eastl::vector<std::unique_ptr<ISDLEventFilter>> filters_;
    bool quit_requested_ = false;
};

}  // namespace tryengine::platform