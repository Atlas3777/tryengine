#pragma once
#include <cstdint>

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
    bool isDown[static_cast<int>(Key::Count)] = {false};
    bool justPressed[static_cast<int>(Key::Count)] = {false};
    bool justReleased[static_cast<int>(Key::Count)] = {false};

    bool IsDown(Key key) const {
        return isDown[static_cast<uint16_t>(key)];
    }

    bool Pressed(Key key) const {
        return justPressed[static_cast<uint16_t>(key)];
    }

    bool Released(Key key) const {
        return justReleased[static_cast<uint16_t>(key)];
    }

    float mouseX = 0.0f, mouseY = 0.0f;
    float mouseDeltaX = 0.0f, mouseDeltaY = 0.0f;

    bool mouseButtons[static_cast<int>(Mouse::Count)] = {false};
    bool mouseJustPressed[static_cast<int>(Mouse::Count)] = {false};
    bool mouseJustReleased[static_cast<int>(Mouse::Count)] = {false};

    // Теперь принимаем Mouse вместо int
    bool IsMouseDown(Mouse button) const {
        return mouseButtons[static_cast<int>(button)];
    }

    bool MousePressed(Mouse button) const {
        return mouseJustPressed[static_cast<int>(button)];
    }

    bool MouseReleased(Mouse button) const {
        return mouseJustReleased[static_cast<int>(button)];
    }

    void ResetFrame() {
        for (int i = 0; i < static_cast<int>(Key::Count); ++i) {
            justPressed[i] = false;
            justReleased[i] = false;
        }

        for (int i = 0; i < static_cast<int>(Mouse::Count); ++i) {
            mouseJustPressed[i] = false;
            mouseJustReleased[i] = false;
        }

        mouseDeltaX = 0.0f;
        mouseDeltaY = 0.0f;
    }
};

} // namespace tryengine::core