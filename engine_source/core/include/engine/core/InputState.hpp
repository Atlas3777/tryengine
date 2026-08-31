#pragma once

#include "Inline.hpp"

namespace tryengine::core {

// Полностью совпадает с USB стандартом (и SDL_Scancode)
enum class Key : uint16_t {
    Unknown = 0,

    // Буквы
    A = 4, B = 5, C = 6, D = 7, E = 8, F = 9, G = 10, H = 11, I = 12,
    J = 13, K = 14, L = 15, M = 16, N = 17, O = 18, P = 19, Q = 20,
    R = 21, S = 22, T = 23, U = 24, V = 25, W = 26, X = 27, Y = 28, Z = 29,

    // Цифры
    Num1 = 30, Num2 = 31, Num3 = 32, Num4 = 33, Num5 = 34,
    Num6 = 35, Num7 = 36, Num8 = 37, Num9 = 38, Num0 = 39,

    // Управление
    Return = 40, Escape = 41, Backspace = 42, Tab = 43, Space = 44,

    // F-клавиши
    F1 = 58, F2 = 59, F3 = 60, F4 = 61, F5 = 62, F6 = 63,
    F7 = 64, F8 = 65, F9 = 66, F10 = 67, F11 = 68, F12 = 69,

    // Стрелки
    Right = 79, Left = 80, Down = 81, Up = 82,

    // Модификаторы
    LCtrl = 224, LShift = 225, LAlt = 226, LGui = 227,
    RCtrl = 228, RShift = 229, RAlt = 230, RGui = 231,

    Count = 512 // Задает размер массива, совпадает с SDL_SCANCODE_COUNT
};

enum class Mouse : uint8_t {
    Left = 0,
    Middle = 1,
    Right = 2,
    X1 = 3,
    X2 = 4,
    Count = 5
};

struct InputState {
    bool is_down[static_cast<int>(Key::Count)] = {false};
    bool just_pressed[static_cast<int>(Key::Count)] = {false};
    bool just_released[static_cast<int>(Key::Count)] = {false};

    __forceinline bool IsDown(Key key) const {
        return is_down[static_cast<uint16_t>(key)];
    }

    __forceinline bool Pressed(Key key) const {
        return just_pressed[static_cast<uint16_t>(key)];
    }

    __forceinline bool Released(Key key) const {
        return just_released[static_cast<uint16_t>(key)];
    }

    float mouse_x = 0.0f, mouse_y = 0.0f;
    float mouse_delta_x = 0.0f, mouse_delta_y = 0.0f;

    bool mouse_buttons[static_cast<int>(Mouse::Count)] = {false};
    bool mouse_just_pressed[static_cast<int>(Mouse::Count)] = {false};
    bool mouse_just_released[static_cast<int>(Mouse::Count)] = {false};

    // Теперь принимаем Mouse вместо int
    __forceinline bool IsMouseDown(Mouse button) const {
        return mouse_buttons[static_cast<int>(button)];
    }

    __forceinline bool MousePressed(Mouse button) const {
        return mouse_just_pressed[static_cast<int>(button)];
    }

    __forceinline bool MouseReleased(Mouse button) const {
        return mouse_just_released[static_cast<int>(button)];
    }

    void ResetFrame() {
        for (int i = 0; i < static_cast<int>(Key::Count); ++i) {
            just_pressed[i] = false;
            just_released[i] = false;
        }

        for (int i = 0; i < static_cast<int>(Mouse::Count); ++i) {
            mouse_just_pressed[i] = false;
            mouse_just_released[i] = false;
        }

        mouse_delta_x = 0.0f;
        mouse_delta_y = 0.0f;
    }
};

} // namespace tryengine::core