#pragma once

#include <EASTL/array.h>
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/variant.h>
#include <EASTL/vector.h>
#include <cstdint>

#include "engine/core/Result.hpp"

namespace tryengine::resources {

// C++-зеркало daslang-модуля tryengine/ConfigFormat. Грамматика, набор типов и
// раскладка полей должны совпадать: файл, записанный C++, читается daslang, и наоборот.
//
// Формат: `name: TYPE = VALUE`; объекты в `{ ... }`, массивы в `[ ... ]`, комментарии
// `//` и `/* ... */`. Полное описание — в engine_source/daslang/ConfigFormat.das.

struct ConfigObject;
struct ConfigArray;
struct ConfigField;

using ConfigFloat2 = eastl::array<float, 2>;
using ConfigFloat3 = eastl::array<float, 3>;
using ConfigFloat4 = eastl::array<float, 4>;

using ConfigInt2 = eastl::array<int32_t, 2>;
using ConfigInt3 = eastl::array<int32_t, 3>;
using ConfigInt4 = eastl::array<int32_t, 4>;

using ConfigUInt2 = eastl::array<uint32_t, 2>;
using ConfigUInt3 = eastl::array<uint32_t, 3>;
using ConfigUInt4 = eastl::array<uint32_t, 4>;

using ConfigFloat3x3 = eastl::array<float, 9>;
using ConfigFloat3x4 = eastl::array<float, 12>;
using ConfigFloat4x4 = eastl::array<float, 16>;

// Значение поля. Порядок альтернатив соответствует ConfigValue в daslang.
using ConfigValue =
    eastl::variant<eastl::monostate,  // null
                   eastl::string, double, float, int64_t, int32_t, int16_t, int8_t, uint64_t, uint32_t, uint16_t,
                   uint8_t, bool, ConfigFloat2, ConfigFloat3, ConfigFloat4, ConfigInt2, ConfigInt3, ConfigInt4,
                   ConfigUInt2, ConfigUInt3, ConfigUInt4, ConfigFloat3x3, ConfigFloat3x4, ConfigFloat4x4,
                   eastl::unique_ptr<ConfigObject>, eastl::unique_ptr<ConfigArray>>;

// Поле конфига: имя, явный тип из текста и значение.
struct ConfigField {
    eastl::string name;
    eastl::string type_name;
    ConfigValue value;

    ConfigField() = default;
    ConfigField(const ConfigField& other);
    ConfigField& operator=(const ConfigField& other);
    ConfigField(ConfigField&&) noexcept = default;
    ConfigField& operator=(ConfigField&&) noexcept = default;
};

// Объект и массив хранят поля в порядке появления. daslang использует table и порядок
// не сохраняет; для C++-записи порядок стабилен и достаточно сравнивать по содержимому.
struct ConfigObject {
    eastl::vector<ConfigField> fields;

    ConfigObject() = default;
    ConfigObject(const ConfigObject& other);
    ConfigObject& operator=(const ConfigObject& other);
    ConfigObject(ConfigObject&&) noexcept = default;
    ConfigObject& operator=(ConfigObject&&) noexcept = default;
};

struct ConfigArray {
    eastl::vector<ConfigField> elements;

    ConfigArray() = default;
    ConfigArray(const ConfigArray& other);
    ConfigArray& operator=(const ConfigArray& other);
    ConfigArray(ConfigArray&&) noexcept = default;
    ConfigArray& operator=(ConfigArray&&) noexcept = default;
};

// Корень документа — объект (набор полей), как результат read_config в daslang.
[[nodiscard]] Result<ConfigObject> ParseConfig(eastl::string_view text);
[[nodiscard]] Result<ConfigObject> ParseConfig(eastl::span<const uint8_t> bytes);

// Записывает документ в грамматике ConfigFormat. Строки экранируются.
[[nodiscard]] eastl::string WriteConfig(const ConfigObject& root);

[[nodiscard]] const ConfigField* FindField(const ConfigObject& object, eastl::string_view name);
[[nodiscard]] ConfigField* FindField(ConfigObject& object, eastl::string_view name);

[[nodiscard]] bool IsObject(const ConfigValue& value);
[[nodiscard]] bool IsArray(const ConfigValue& value);
[[nodiscard]] const ConfigObject* GetObject(const ConfigValue& value);
[[nodiscard]] const ConfigArray* GetArray(const ConfigValue& value);
[[nodiscard]] ConfigObject* GetObject(ConfigValue& value);
[[nodiscard]] ConfigArray* GetArray(ConfigValue& value);

}  // namespace tryengine::resources
