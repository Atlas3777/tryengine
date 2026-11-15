#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <cstdint>

namespace tryengine::resources {

// Авторская ссылка на ассет. Одна строка, два вида:
//   "game/data/materials/stone.mat" — авторитетный файл, идентичность = путь;
//   "guid:17036954862819815981"      — импортируемый ассет, идентичность = guid из .meta.
// Вид ссылки — это данные, а не тип поля: в компоненте лежит обычный string.
inline constexpr eastl::string_view kGuidRefPrefix = "guid:";

[[nodiscard]] inline bool IsGuidRef(eastl::string_view ref) {
    return ref.size() > kGuidRefPrefix.size() && ref.substr(0, kGuidRefPrefix.size()) == kGuidRefPrefix;
}

[[nodiscard]] inline bool ParseGuidRef(eastl::string_view ref, uint64_t& out) {
    if (!IsGuidRef(ref))
        return false;
    uint64_t value = 0;
    for (size_t i = kGuidRefPrefix.size(); i < ref.size(); ++i) {
        const char c = ref[i];
        if (c < '0' || c > '9')
            return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

[[nodiscard]] inline eastl::string MakeGuidRef(uint64_t id) {
    char digits[20];
    int count = 0;
    do {
        digits[count++] = static_cast<char>('0' + id % 10);
        id /= 10;
    } while (id != 0);

    eastl::string ref(kGuidRefPrefix.data(), kGuidRefPrefix.size());
    ref.reserve(ref.size() + static_cast<size_t>(count));
    while (count > 0)
        ref.push_back(digits[--count]);
    return ref;
}

}  // namespace tryengine::resources
