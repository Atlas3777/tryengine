#pragma once

#include <cstdint>

namespace tryeditor {

enum class PlayModeState : uint8_t {
    Edit,
    Play,
    Pause,
};

}