#pragma once

#include <random>
#include <type_traits>

namespace tryengine::core {
namespace random {

inline std::mt19937_64& GetEngine() {
    thread_local std::random_device rd;
    thread_local std::mt19937_64 gen(rd());
    return gen;
}

inline uint64_t GenerateInt64() {
    std::uniform_int_distribution<uint64_t> dis;
    return dis(GetEngine());
}

// Универсальная функция Random с поддержкой диапазона [min, max]
template <typename T>
inline T Random(T min, T max) {
    if constexpr (std::is_integral_v<T>) {
        std::uniform_int_distribution<T> dis(min, max);
        return dis(GetEngine());
    } else if constexpr (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<T> dis(min, max);
        return dis(GetEngine());
    }
    TRY_ASSERT(false, "Typy myst by is_integral_v or std::is_floating_point_v");
    return 0;
}

inline uint64_t CombineID(uint64_t main_uuid, eastl::string_view base_name) {
    constexpr uint64_t kFNVOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kFNVPrime = 1099511628211ULL;

    uint64_t hash = kFNVOffsetBasis;

    const auto uuid_bytes = reinterpret_cast<const uint8_t*>(&main_uuid);
    for (size_t i = 0; i < sizeof(main_uuid); ++i) {
        hash ^= uuid_bytes[i];
        hash *= kFNVPrime;
    }

    for (const char c : base_name) {
        hash ^= static_cast<uint8_t>(c);
        hash *= kFNVPrime;
    }

    return hash;
}

}  // namespace random
}  // namespace tryengine::core