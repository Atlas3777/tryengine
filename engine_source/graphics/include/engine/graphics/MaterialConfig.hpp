#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <cstdint>

#include "engine/core/Result.hpp"
#include "engine/graphics/ShaderReflection.hpp"
#include "engine/resources/ConfigFormat.hpp"

namespace tryengine::graphics {

// Материал как данные, заполняющие шейдерный шаблон. Поля `params` и `textures`
// динамические: их имена и типы задаёт рефлексия Slang, поэтому C++ не знает конкретный
// набор параметров (albedo_color, roughness, ...) и не должен его знать.
//
// C++-зеркало daslang-структуры `MaterialConfig` из editor/daslang/Material.das:
// обе стороны пишут один и тот же ConfigFormat.
struct MaterialConfig {
    eastl::string name;
    uint64_t shader = 0;
    // Uniform-параметры из UBO (set 1): имя поля = имя параметра в рефлексии.
    resources::ConfigObject params;
    // Сэмплируемые текстуры (set 1): имя поля = имя binding, значение — u64 id текстуры.
    resources::ConfigObject textures;
};

[[nodiscard]] Result<MaterialConfig> ParseMaterialConfig(eastl::span<const uint8_t> bytes);
[[nodiscard]] Result<MaterialConfig> ParseMaterialConfig(eastl::string_view text);

[[nodiscard]] eastl::string WriteMaterialConfig(const MaterialConfig& config);

// Схема параметров материала из рефлексии шейдера. Возвращает ConfigFormat-текст:
//
//   params: MaterialParam[] = [
//       { name: string = "..."; type: string = "f32x4"; kind: string = "uniform"; ... }
//   ]
//
// `type` — тип хранения в ConfigFormat (у текстур это `u64`), `kind` — `uniform` или
// `texture`: по нему редактор раскладывает поля в `params` или `textures`.
// Нужна редактору, чтобы нарисовать поля материала; в самой игре не используется.
[[nodiscard]] eastl::string BuildMaterialSchemaConfig(const ShaderReflectionData& reflection);

[[nodiscard]] const char* ShaderParamTypeToConfigType(ShaderParamType type);

}  // namespace tryengine::graphics
