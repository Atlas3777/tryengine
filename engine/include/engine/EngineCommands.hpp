#pragma once
#include <variant>

namespace engine {
enum class PresentMode {
    Immediate,  // Максимальный FPS, возможны разрывы (Tearing)
    Mailbox,    // Высокий FPS, без разрывов (Triple Buffering)
    VSync       // Ограничен частотой монитора (Double Buffering / FIFO)
};

struct CmdSetCursorCapture {
    bool enable;
};
struct CmdSetPresentMode {
    PresentMode mode;
    explicit CmdSetPresentMode(const PresentMode m) : mode(m) {}
};

struct CmdSetVSync {
    bool enable;
};

struct CmdSetFullscreen {
    bool enable;
};

struct CmdToggleCursorCapture {
    // Пустая структура — просто сигнал переключить состояние
};

struct CmdQuit {
    // Сигнал к закрытию движка
};

using EngineCommand =
    std::variant<CmdSetCursorCapture, CmdSetPresentMode, CmdSetFullscreen, CmdToggleCursorCapture, CmdQuit>;
} // namespace engine
