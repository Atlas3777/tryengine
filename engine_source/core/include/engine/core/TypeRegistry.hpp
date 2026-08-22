#pragma once

#include <cstdint>

namespace tryengine::core {

template <typename Scope>
struct ScopedTypeCounter {
    static inline uint32_t counter = 0;
};

template <typename Scope, typename Type>
struct ScopedTypeId {
    static uint32_t Value() noexcept {
        static const uint32_t id = ScopedTypeCounter<Scope>::counter++;
        return id;
    }
};

} // namespace tryengine::core