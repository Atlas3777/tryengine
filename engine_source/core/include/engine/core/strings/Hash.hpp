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

// Ключ ресурса. '\' == '/', регистр ASCII не важен.
// ОДНА функция и в редакторе, и в рантайме билда.
// constexpr: `constexpr uint64_t k = PathHash("textures/a.png");` — строки в бинарнике нет.
constexpr uint64_t PathHash(eastl::string_view s) noexcept {
    uint64_t h = kFnvOffset;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\')
            c = '/';
        else if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
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