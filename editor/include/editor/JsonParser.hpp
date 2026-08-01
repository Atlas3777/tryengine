#pragma once

#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <glaze/json.hpp>

#include "engine/core/Assert.hpp"

namespace tryeditor {

template <typename T>
eastl::optional<T> Deserialize(const eastl::span<const char> buffer) {
    T obj;
    auto ec = glz::read_json(obj, buffer);

    if (ec) {
        TRY_LOG_ERROR("Ошибка при десериализации {}", ec.custom_error_message);
        return eastl::nullopt;
    }

    return obj;
}

template <typename T>
eastl::optional<eastl::vector<char>> Serialize(const T& obj) {
    eastl::vector<char> buffer;

    const auto ec = glz::write_json(obj, buffer);
    if (ec) {
        TRY_LOG_ERROR("Ошибка при сериализации {}", ec.custom_error_message);
        return eastl::nullopt;
    }

    return buffer;
}

template <typename T>
eastl::optional<T> DeserializePartial(const eastl::span<const char> buffer)
{
    T obj;

    auto ec = glz::read<
        glz::opts{
            .error_on_unknown_keys = false,
            .partial_read = true
        }>(obj, buffer);

    if (ec) {
        TRY_LOG_ERROR("Ошибка при десериализации {}", ec.custom_error_message);
        return eastl::nullopt;
    }

    return obj;
}

}  // namespace tryeditor