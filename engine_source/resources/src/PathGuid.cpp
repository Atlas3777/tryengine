#include "engine/resources/PathGuid.hpp"

#include <cstddef>

namespace tryengine::resources {

namespace {

// Нормализуем только то, что не требует файловой системы: разделители, ведущий
// "./" и повторные "/". Путь остаётся относительным корня проекта.
eastl::string NormalizePath(eastl::string_view path) {
    eastl::string normalized;
    normalized.reserve(path.size());

    for (const char ch : path) {
        const char fixed = ch == '\\' ? '/' : ch;
        if (fixed == '/' && !normalized.empty() && normalized.back() == '/') {
            continue;
        }
        normalized.push_back(fixed);
    }

    while (normalized.size() >= 2 && normalized[0] == '.' && normalized[1] == '/') {
        normalized.erase(0, 2);
    }
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    return normalized;
}

}  // namespace

uint64_t PathGuid(eastl::string_view path) {
    const eastl::string normalized = NormalizePath(path);

    // FNV-1a с солью: путь -> стабильный 64-битный id. Коллизия с guid из .meta
    // теоретически возможна, но практически исключена; для data/-файлов достаточно.
    uint64_t hash = 1469598103934665603ull;
    constexpr uint64_t kSalt = 0x9E3779B97F4A7C15ull;
    hash ^= kSalt;
    hash *= 1099511628211ull;

    for (const char ch : normalized) {
        hash ^= static_cast<uint8_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace tryengine::resources
