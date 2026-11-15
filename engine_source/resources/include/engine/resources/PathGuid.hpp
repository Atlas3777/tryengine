#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <cstdint>

namespace tryengine::resources {

// Стабильный идентификатор ресурса, у которого нет .meta/GUID (файлы в data/).
// Считается из нормализованного пути, поэтому одинаков между запусками и не требует
// записи в файл. Используется только для материалов/ресурсов, адресуемых путём.
[[nodiscard]] uint64_t PathGuid(eastl::string_view path);

}  // namespace tryengine::resources
