#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <cstdint>

namespace tryengine::core {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

// Точный хеш байтов. Для внутренних таблиц (StringTable): "A.png" != "a.png".
constexpr uint64_t Hash64(eastl::string_view s) noexcept {
    uint64_t h = kFnvOffset;
    for (size_t i = 0; i < s.size(); ++i) {
        h ^= static_cast<uint8_t>(s[i]);
        h *= kFnvPrime;
    }
    return h;
}

// Идентификатор ресурса из пути. ОДНА функция и в редакторе, и в рантайме билда.
// Нормализация: '\' == '/', подряд идущие '/' схлопываются, ведущий "./" и хвостовой '/'
// отбрасываются. Регистр значим: на Linux "Foo.mat" и "foo.mat" — разные файлы.
// Значения совпадают с прежним PathGuid, поэтому уже сохранённые ссылки на .mat живут.
// constexpr: `constexpr uint64_t k = PathId("textures/a.png");` — строки в бинарнике нет.
constexpr uint64_t kPathIdBasis = 1469598103934665603ull;
constexpr uint64_t kPathIdSalt = 0x9E3779B97F4A7C15ull;

constexpr uint64_t PathId(eastl::string_view s) noexcept {
    size_t start = 0;
    while (start + 1 < s.size() && s[start] == '.' && (s[start + 1] == '/' || s[start + 1] == '\\'))
        start += 2;

    uint64_t h = kPathIdBasis;
    h ^= kPathIdSalt;
    h *= kFnvPrime;

    bool slash_pending = false;
    for (size_t i = start; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '/' || c == '\\') {
            slash_pending = true;
            continue;
        }
        if (slash_pending) {
            h ^= static_cast<uint8_t>('/');
            h *= kFnvPrime;
            slash_pending = false;
        }
        h ^= static_cast<uint8_t>(c);
        h *= kFnvPrime;
    }
    return h;
}

// Мост на границе со старым API, где ещё живёт eastl::string.
inline eastl::string_view ToView(const eastl::string& s) {
    return eastl::string_view(s.data(), s.size());
}

}  // namespace tryengine::core