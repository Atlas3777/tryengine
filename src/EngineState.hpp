#pragma once
#include <cstdint>

#include "EngineCommands.hpp"
#include "EngineTypes.hpp"
#include "SDL3/SDL_scancode.h"
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
