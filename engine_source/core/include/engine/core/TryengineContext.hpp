#pragma once

#include <daScript/daScript.h>

#include "Engine.hpp"

namespace tryengine::core {

class TryengineContext : public das::Context {
public:
    const Engine& engine;

    TryengineContext(const Engine& eng, uint32_t stack_size = 16 * 1024, bool ph = false)
        : Context(stack_size, ph), engine(eng) {}
};
}  // namespace tryengine::core