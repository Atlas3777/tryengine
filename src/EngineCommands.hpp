#pragma once
#include <variant>

// В файле с определениями команд/настроек
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
    CmdSetPresentMode(PresentMode m) : mode(m) {}
};

// 1. Описываем конкретные команды как простые структуры данных (POD)
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

// 2. Объединяем их в один безопасный тип.
// variant хранит внутри только один из этих типов в данный момент времени.
using EngineCommand =
    std::variant<CmdSetCursorCapture, CmdSetPresentMode, CmdSetFullscreen, CmdToggleCursorCapture, CmdQuit>;
