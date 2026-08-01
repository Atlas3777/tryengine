#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <format>

namespace tryengine::fmt {

//============================================================
// Append formatting
//============================================================

template <class Alloc, class... Args>
void format_append(eastl::basic_string<char, Alloc>& str, std::format_string<Args...> fmt, Args&&... args) {
    const size_t oldSize = str.size();
    const size_t appendSize = std::formatted_size(fmt, std::forward<Args>(args)...);

    str.resize(oldSize + appendSize);

    std::format_to(str.data() + oldSize, fmt, std::forward<Args>(args)...);
}

//============================================================
// Overwrite formatting
//============================================================

template <class Alloc, class... Args>
void format_to(eastl::basic_string<char, Alloc>& str, std::format_string<Args...> fmt, Args&&... args) {
    const size_t size = std::formatted_size(fmt, std::forward<Args>(args)...);

    str.resize(size);

    std::format_to(str.data(), fmt, std::forward<Args>(args)...);
}

//============================================================
// Create string with allocator
//============================================================

template <class Alloc, class... Args>
[[nodiscard]]
eastl::basic_string<char, Alloc> format_alloc(const Alloc& alloc, std::format_string<Args...> fmt, Args&&... args) {
    eastl::basic_string<char, Alloc> result(alloc);
    format_to(result, fmt, std::forward<Args>(args)...);
    return result;
}

//============================================================
// Default formatter
//============================================================

template <class... Args>
[[nodiscard]]
eastl::string format(std::format_string<Args...> fmt, Args&&... args) {
    eastl::string result;
    format_to(result, fmt, std::forward<Args>(args)...);
    return result;
}

//============================================================
// HybridFormat
//
// До StackSize символов — стек.
// После — EASTL string.
//============================================================

template <size_t StackSize = 256, class Alloc = EASTLAllocatorType>
class HybridFormat {
public:
    HybridFormat() = default;

    template <class... Args>
    explicit HybridFormat(std::format_string<Args...> fmt, Args&&... args) {
        assign(fmt, std::forward<Args>(args)...);
    }

    template <class... Args>
    HybridFormat(const Alloc& alloc, std::format_string<Args...> fmt, Args&&... args) : m_heapString(alloc) {
        assign(fmt, std::forward<Args>(args)...);
    }

    HybridFormat(const HybridFormat&) = delete;
    HybridFormat& operator=(const HybridFormat&) = delete;

    HybridFormat(HybridFormat&&) noexcept = default;
    HybridFormat& operator=(HybridFormat&&) noexcept = default;

public:
    template <class... Args>
    void assign(std::format_string<Args...> fmt, Args&&... args) {
        const size_t needed = std::formatted_size(fmt, std::forward<Args>(args)...);

        m_length = needed;

        if (needed < StackSize) {
            auto result = std::format_to_n(m_stackBuffer, StackSize - 1, fmt, std::forward<Args>(args)...);

            *result.out = '\0';

            m_heap = false;
        } else {
            m_heapString.resize(needed);

            std::format_to(m_heapString.data(), fmt, std::forward<Args>(args)...);

            m_heap = true;
        }
    }

    void clear() {
        m_length = 0;
        m_heap = false;

        m_stackBuffer[0] = '\0';
        m_heapString.clear();
    }

    [[nodiscard]]
    bool empty() const noexcept {
        return m_length == 0;
    }

    [[nodiscard]]
    bool is_heap_allocated() const noexcept {
        return m_heap;
    }

    [[nodiscard]]
    size_t size() const noexcept {
        return m_length;
    }

    [[nodiscard]]
    const char* data() const noexcept {
        return m_heap ? m_heapString.data() : m_stackBuffer;
    }

    [[nodiscard]]
    const char* c_str() const noexcept {
        return m_heap ? m_heapString.c_str() : m_stackBuffer;
    }

    [[nodiscard]]
    eastl::string_view sv() const noexcept {
        return {data(), size()};
    }

    operator const char*() const noexcept { return c_str(); }

    operator eastl::string_view() const noexcept { return sv(); }

private:
    bool m_heap = false;
    size_t m_length = 0;
    char m_stackBuffer[StackSize]{};
    eastl::basic_string<char, Alloc> m_heapString;
};

using StringFormat = HybridFormat<256>;

}  // namespace tryengine::fmt