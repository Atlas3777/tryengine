#pragma once

#include <EASTL/span.h>

#include "editor/JsonParser.hpp"
#include "engine/async/Task.hpp"

namespace tryeditor {

template <typename T>
class JsonLoader {
public:
    tryengine::async::Task<T> Parse(eastl::span<const uint8_t> bytes) {
        if (bytes.empty()) {
            co_return tryengine::core::Error("Buffer empty");
        }

        auto result = Deserialize<T>(bytes);
        if (!result.has_value()) {
            co_return tryengine::core::Error("Error deserializing");
        }

        co_return std::move(result.value());
    }
};

} // namespace tryeditor