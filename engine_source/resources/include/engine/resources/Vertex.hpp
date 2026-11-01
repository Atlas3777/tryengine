#pragma once

#include <cstdint>

namespace tryengine::resources {

enum class IndexFormat : uint8_t {
    None,
    UInt8,
    UInt16,
    UInt32
};

}  // namespace tryengine::resources