#pragma once

#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <cstring>
#include <type_traits>

#include "engine/core/Assert.hpp"

namespace tryeditor {

template <typename T>
eastl::optional<eastl::vector<uint8_t>> BinarySerialize(const T& obj) {
    static_assert(std::is_trivially_copyable_v<T>, "BinarySerialize: Type must be trivially copyable");

    eastl::vector<uint8_t> buffer(sizeof(T));
    std::memcpy(buffer.data(), &obj, sizeof(T));

    return buffer;
}

template <typename T>
eastl::optional<T> BinaryDeserialize(const eastl::span<const uint8_t> buffer) {
    static_assert(std::is_trivially_copyable_v<T>, "BinaryDeserialize: Type must be trivially copyable");

    if (buffer.size() < sizeof(T)) {
        LogError("Ошибка при бинарной десериализации: недостаточный размер буфера (ожидалось {} байт, получено {})", sizeof(T), buffer.size());
        return eastl::nullopt;
    }

    T obj;
    std::memcpy(&obj, buffer.data(), sizeof(T));

    return obj;
}

}  // namespace tryeditor