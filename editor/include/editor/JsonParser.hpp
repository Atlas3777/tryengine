#pragma once

#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <glaze/json.hpp>

#include "engine/core/Assert.hpp"
#include "engine/resources/EastlGlazeSerializers.hpp"
#include "engine/resources/GLMSerialization.hpp"

namespace tryeditor {

template <typename T>
eastl::optional<T> Deserialize(const eastl::span<const uint8_t> buffer) {
    T obj;
    auto ec = glz::read_json(obj, buffer);

    if (ec) {
        LogError("Ошибка при десериализации {}", ec.custom_error_message);
        return eastl::nullopt;
    }

    return obj;
}

template <typename T>
eastl::optional<eastl::vector<uint8_t>> Serialize(const T& obj) {
    eastl::vector<uint8_t> buffer;

    const auto ec = glz::write_json(obj, buffer);
    if (ec) {
        LogError("Ошибка при сериализации {}", ec.custom_error_message);
        return eastl::nullopt;
    }

    return buffer;
}

template <typename T>
eastl::optional<T> DeserializePartial(const eastl::span<const uint8_t> buffer) {
    T obj;

    auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(obj, buffer);

    if (ec) {
        LogError("Ошибка при частичной десериализации {}", ec.custom_error_message);
        return eastl::nullopt;
    }

    return obj;
}

}  // namespace tryeditor