#pragma once
#include <cstdint>

#include "SDL3/SDL_scancode.h"

struct EngineSettings {
    bool isEditorMode = true;
    bool vSyncEnable = true;
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
    bool keyPressed[SDL_SCANCODE_COUNT] = {false};
    const bool* keyboardState = nullptr;

    bool IsKeyDown(SDL_Scancode code) const { return keyboardState && keyboardState[code]; }
    bool WasKeyPressed(SDL_Scancode code) const { return keyPressed[code]; }
};
