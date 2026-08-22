// EastlGlazeSerializers.hpp
#pragma once

#include <glaze/glaze.hpp>
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <unordered_map>
#include <vector>
#include <string>

namespace glz {

// ==========================================
// 1. eastl::basic_string (eastl::string)
// ==========================================
template <typename CharT>
struct from<JSON, eastl::basic_string<CharT>> {
    template <auto Opts>
    static void op(eastl::basic_string<CharT>& value, is_context auto&& ctx, auto&& it, auto&& end) {
        std::string temp;
        parse<JSON>::op<Opts>(temp, ctx, it, end);
        value = eastl::basic_string<CharT>(temp.c_str(), temp.size());
    }
};

template <typename CharT>
struct to<JSON, eastl::basic_string<CharT>> {
    template <auto Opts>
    static void op(const eastl::basic_string<CharT>& value, is_context auto&& ctx, auto&& b, auto&& ix) noexcept {
        std::string temp(value.c_str(), value.size());
        serialize<JSON>::op<Opts>(temp, ctx, b, ix);
    }
};

// ==========================================
// 2. eastl::vector
// ==========================================
template <typename T, typename Alloc>
struct from<JSON, eastl::vector<T, Alloc>> {
    template <auto Opts>
    static void op(eastl::vector<T, Alloc>& value, is_context auto&& ctx, auto&& it, auto&& end) {
        std::vector<T> temp;
        parse<JSON>::op<Opts>(temp, ctx, it, end);

        value.clear();
        value.reserve(temp.size());
        for (auto& item : temp) {
            value.push_back(std::move(item));
        }
    }
};

template <typename T, typename Alloc>
struct to<JSON, eastl::vector<T, Alloc>> {
    template <auto Opts>
    static void op(const eastl::vector<T, Alloc>& value, is_context auto&& ctx, auto&& b, auto&& ix) noexcept {
        std::vector<T> temp(value.begin(), value.end());
        serialize<JSON>::op<Opts>(temp, ctx, b, ix);
    }
};

// ==========================================
// 3. eastl::hash_map (он же eastl::unordered_map)
// ==========================================
// Специализируем под ключ eastl::string, так как JSON-объекты требуют строковые ключи.
template <typename V, typename Hash, typename Eq, typename Alloc>
struct from<JSON, eastl::hash_map<eastl::string, V, Hash, Eq, Alloc>> {
    template <auto Opts>
    static void op(eastl::hash_map<eastl::string, V, Hash, Eq, Alloc>& value, is_context auto&& ctx, auto&& it, auto&& end) {
        std::unordered_map<std::string, V> temp;
        parse<JSON>::op<Opts>(temp, ctx, it, end);

        value.clear();
        value.reserve(temp.size());
        for (auto& [k, v] : temp) {
            value.emplace(eastl::string(k.c_str(), k.size()), std::move(v));
        }
    }
};

template <typename V, typename Hash, typename Eq, typename Alloc>
struct to<JSON, eastl::hash_map<eastl::string, V, Hash, Eq, Alloc>> {
    template <auto Opts>
    static void op(const eastl::hash_map<eastl::string, V, Hash, Eq, Alloc>& value, is_context auto&& ctx, auto&& b, auto&& ix) noexcept {
        std::unordered_map<std::string, V> temp;
        temp.reserve(value.size());
        for (const auto& [k, v] : value) {
            temp.emplace(std::string(k.c_str(), k.size()), v);
        }
        serialize<JSON>::op<Opts>(temp, ctx, b, ix);
    }
};

} // namespace glz