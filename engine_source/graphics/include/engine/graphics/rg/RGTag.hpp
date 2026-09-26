#pragma once

#include <EASTL/string_view.h>
#include <algorithm>
#include <cstdint>

#ifndef NDEBUG
#include <EASTL/unordered_map.h>
#include <EASTL/string.h>
#include <mutex>
#endif

namespace tryengine::graphics {

using RGTag = uint32_t;

constexpr RGTag RGHashFNV1a(eastl::string_view s) {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

template <size_t N>
struct FixedString {
    char data[N]{};
    constexpr FixedString(const char (&s)[N]) {
        std::copy_n(s, N, data);
    }
};

#ifndef NDEBUG
namespace detail {

// Мьютекс и глобальная таблица имён для отладки
inline auto& GetRGTagRegistry() {
    static eastl::unordered_map<RGTag, eastl::string> registry;
    return registry;
}

inline std::mutex& GetRGTagMutex() {
    static std::mutex mutex;
    return mutex;
}

// Функция потокобезопасной регистрации имени
inline void RegisterRGTag(RGTag tag, eastl::string_view name) {
    std::lock_guard<std::mutex> lock(GetRGTagMutex());
    // Сохраняем как копию eastl::string на случай,
    // если в RGTagOf передадут временный string_view
    GetRGTagRegistry()[tag] = eastl::string(name.data(), name.size());
}

// Вспомогательная структура для авто-регистрации constexpr-тегов при инициализации
template <FixedString S>
struct TagRegistrar {
    static inline const RGTag tag = []() {
        constexpr eastl::string_view view(S.data, sizeof(S.data) - 1);
        constexpr RGTag t = RGHashFNV1a(view);
        RegisterRGTag(t, view);
        return t;
    }();
};

}  // namespace detail

// Функция получения имени по хэшу (доступна только в Debug)
inline eastl::string_view GetRGTagName(RGTag tag) {
    std::lock_guard<std::mutex> lock(detail::GetRGTagMutex());
    auto& reg = detail::GetRGTagRegistry();
    auto it = reg.find(tag);
    if (it != reg.end()) {
        return eastl::string_view(it->second.data(), it->second.size());
    }
    return "Unknown Tag";
}

#else

// В Release функция возвращает пустую строку и стирается оптимизатором
inline constexpr eastl::string_view GetRGTagName(RGTag) {
    return "use debug for info";
}

#endif

// Compile-time тег
template <FixedString S>
#ifndef NDEBUG
inline const RGTag RGTag_v = detail::TagRegistrar<S>::tag;
#else
inline constexpr RGTag RGTag_v = RGHashFNV1a(eastl::string_view(S.data, sizeof(S.data) - 1));
#endif

// Runtime тег
inline RGTag RGTagOf(eastl::string_view name) {
    RGTag tag = RGHashFNV1a(name);
#ifndef NDEBUG
    detail::RegisterRGTag(tag, name);
#endif
    return tag;
}

}  // namespace tryengine::graphics