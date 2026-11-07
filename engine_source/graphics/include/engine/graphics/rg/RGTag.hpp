#pragma once

#include <EASTL/string_view.h>
#include <algorithm>
#ifndef NDEBUG
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <mutex>
#endif

namespace tryengine::graphics {

using RGTag = uint32_t;
using ScopeId = uint32_t;
constexpr ScopeId kGlobalScope = 0;

// Упаковываем View и Tag в 64 бита для молниеносного хэширования
using RGTagKey = uint64_t;
inline constexpr RGTagKey MakeTagKey(ScopeId view, RGTag tag) {
    return (static_cast<uint64_t>(view) << 32) | static_cast<uint64_t>(tag);
}
inline constexpr RGTag GetTagFromKey(RGTagKey key) {
    return static_cast<RGTag>(key & 0xFFFFFFFF);
}
inline constexpr ScopeId GetViewFromKey(RGTagKey key) {
    return static_cast<ScopeId>(key >> 32);
}

struct RGScopedTag {
    RGTag tag;
    ScopeId view;
};
inline constexpr RGScopedTag At(RGTag tag, ScopeId view) {
    return {tag, view};
}

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
    constexpr FixedString(const char (&s)[N]) { std::copy_n(s, N, data); }
};

#ifndef NDEBUG
namespace detail {
inline auto& GetRGTagRegistry() {
    static eastl::unordered_map<RGTag, eastl::string> registry;
    return registry;
}
inline std::mutex& GetRGTagMutex() {
    static std::mutex mutex;
    return mutex;
}
inline void RegisterRGTag(RGTag tag, eastl::string_view name) {
    std::lock_guard<std::mutex> lock(GetRGTagMutex());
    GetRGTagRegistry()[tag] = eastl::string(name.data(), name.size());
}
template <FixedString S>
struct TagRegistrar {
    static inline const RGTag tag = [] {
        constexpr eastl::string_view view(S.data, sizeof(S.data) - 1);
        constexpr RGTag t = RGHashFNV1a(view);
        RegisterRGTag(t, view);
        return t;
    }();
};
}  // namespace detail

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
inline constexpr eastl::string_view GetRGTagName(RGTag) {
    return "use debug for info";
}
#endif

template <FixedString S>
#ifndef NDEBUG
inline const RGTag RGTag_v = detail::TagRegistrar<S>::tag;
#else
inline constexpr RGTag RGTag_v = RGHashFNV1a(eastl::string_view(S.data, sizeof(S.data) - 1));
#endif

inline RGTag RGTagOf(eastl::string_view name) {
    RGTag tag = RGHashFNV1a(name);
#ifndef NDEBUG
    detail::RegisterRGTag(tag, name);
#endif
    return tag;
}

}  // namespace tryengine::graphics