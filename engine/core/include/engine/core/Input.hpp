#pragma once
#include <SDL3/SDL_scancode.h>

namespace engine::core {

enum class MouseButton : uint8_t {
    Left = 1,    // Соответствует SDL_BUTTON_LEFT
    Middle = 2,  // Соответствует SDL_BUTTON_MIDDLE
    Right = 3,   // Соответствует SDL_BUTTON_RIGHT
    X1 = 4,
    X2 = 5,
    Count
};
struct InputState {
    bool isCursorCaptured = true;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;

    float mouseX = 0.0f;
    float mouseY = 0.0f;

    bool keyPressed[SDL_SCANCODE_COUNT] = {false};
    const bool* keyboardState = nullptr;

    bool IsKeyDown(SDL_Scancode code) const { return keyboardState && keyboardState[code]; }
    bool WasKeyPressed(SDL_Scancode code) const { return keyPressed[code]; }

    bool isMouseDown[static_cast<int>(MouseButton::Count)] = {false};

    bool IsMouseButtonDown(MouseButton button) const { return isMouseDown[static_cast<int>(button)]; }
};
}  // namespace engine
