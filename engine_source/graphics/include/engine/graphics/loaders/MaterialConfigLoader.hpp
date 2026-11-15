#pragma once

#include <EASTL/span.h>

#include "engine/async/Task.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/graphics/MaterialConfig.hpp"

namespace tryengine::graphics {

// Данные материала как ресурс: редактору нужны шейдер и текстуры материала, чтобы
// собрать зависимости пака. Грузятся тем же ParseMaterialConfig, что и GPU-материал,
// поэтому формат остаётся один.
class MaterialConfigLoader {
public:
    async::Task<MaterialConfig> Parse(eastl::span<const uint8_t> bytes) const {
        if (bytes.empty())
            co_return core::Error("Buffer empty");

        auto result = ParseMaterialConfig(bytes);
        if (!result.has_value())
            co_return LogAndMakeError("Material config: {}", result.error());

        co_return eastl::move(*result);
    }
};

}  // namespace tryengine::graphics
