#pragma once
#include <variant>

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
using EngineCommand = std::variant<CmdSetVSync, CmdSetFullscreen, CmdToggleCursorCapture, CmdQuit>;
