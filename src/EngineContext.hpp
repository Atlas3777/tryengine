#pragma once

#include <cstdint>

#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_scancode.h"

struct EngineContext {
    bool isEditorModeEnable = true;
    bool vSynvEnable = true;
    bool fullScreenEnable = true;
    bool isCursorCaptured = true;
    const bool* keyboardState = nullptr;
    bool running = true;

    // 2. Одноразовые нажатия (импульсы)
    // Мы будем использовать std::bitset или простой массив, чтобы не усложнять
    bool keyPressed[SDL_SCANCODE_COUNT] = {false};

    bool IsKeyDown(SDL_Scancode code) const { return keyboardState[code]; }
    bool WasKeyPressed(SDL_Scancode code) const { return keyPressed[code]; }
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    uint32_t mouseButtons = 0;  // Битовая маска кнопок

    // Вспомогательные методы
    bool IsMouseButtonDown(uint8_t button) const { return mouseButtons & SDL_BUTTON_MASK(button); }
};
