#pragma once

#include <EASTL/string_view.h>
#include <algorithm>
#include <cstdint>

namespace tryengine::graphics {

using RGTag = uint32_t;

constexpr RGTag RGHashFNV1a(eastl::string_view s) {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

template <size_t N>
struct FixedString {
    char data[N]{};
    constexpr FixedString(const char (&s)[N]) {
        std::copy_n(s, N, data);
    }
};

template <FixedString S>
inline constexpr RGTag RGTag_v = RGHashFNV1a(eastl::string_view(S.data, sizeof(S.data) - 1));

inline constexpr RGTag RGTagOf(eastl::string_view name) {
    return RGHashFNV1a(name);
}

}  // namespace tryengine::graphics