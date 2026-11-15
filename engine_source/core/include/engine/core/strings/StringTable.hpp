#pragma once

#include <EASTL/hash_set.h>
#include <EASTL/string_view.h>
#include <cstring>
#include <mutex>

#include "engine/core/strings/Hash.hpp"
#include "engine/core/strings/StringArena.hpp"

namespace tryengine::core {

// Интернированные строки с refcount.
//  - Intern(s) -> const char*: стабильный указатель, null-terminated, пока есть хоть один владелец.
//  - Одинаковые строки хранятся один раз; равенство двух интернированных строк == равенство указателей.
//  - Release(p): refs-- ; при 0 память возвращается в арену (указатель после этого невалиден).
// Потокобезопасна (один мьютекс). Для daslang наружу уходит обычный const char*.
class StringTable {
public:
    StringTable() = default;
    StringTable(const StringTable&) = delete;
    StringTable& operator=(const StringTable&) = delete;

    const char* Intern(eastl::string_view s) {
        const uint64_t h = Hash64(s);
        std::lock_guard<std::mutex> lk(mutex_);

        auto it =
            set_.find_as(s, [h](const eastl::string_view&) -> size_t { return static_cast<size_t>(h); }, EntryEq{});
        if (it != set_.end()) {
            ++Hdr(*it)->refs;
            return *it;
        }

        auto* hdr = static_cast<Header*>(arena_.Alloc(static_cast<uint32_t>(sizeof(Header) + s.size() + 1)));
        hdr->hash = h;
        hdr->len = static_cast<uint32_t>(s.size());
        hdr->refs = 1;
        char* data = reinterpret_cast<char*>(hdr + 1);
        if (!s.empty())
            std::memcpy(data, s.data(), s.size());
        data[s.size()] = '\0';
        set_.insert(data);
        return data;
    }

    const char* AddRef(const char* s) {
        std::lock_guard<std::mutex> lk(mutex_);
        ++Hdr(s)->refs;
        return s;
    }

    void Release(const char* s) {
        if (!s)
            return;
        std::lock_guard<std::mutex> lk(mutex_);
        Header* h = Hdr(s);
        if (--h->refs != 0)
            return;
        set_.erase(s);
        arena_.Free(h, static_cast<uint32_t>(sizeof(Header) + h->len + 1));
    }

    static uint32_t Length(const char* s) {
        return Hdr(s)->len;
    }

    static eastl::string_view View(const char* s) {
        return eastl::string_view(s, Hdr(s)->len);
    }

    // Количество живых уникальных строк (для тестов на утечки).
    size_t Count() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return set_.size();
    }

private:
    struct Header {
        uint64_t hash;
        uint32_t len;
        uint32_t refs;
    };  // 16 байт => данные выровнены по 16

    static_assert(sizeof(Header) == 16);

    static Header* Hdr(const char* s) {
        return reinterpret_cast<Header*>(const_cast<char*>(s)) - 1;
    }

    struct EntryHash {
        size_t operator()(const char* s) const {
            return static_cast<size_t>(Hdr(s)->hash);
        }
    };

    struct EntryEq {
        bool operator()(const char* a, const char* b) const {
            return a == b;
        }

        bool operator()(const char* a, const eastl::string_view& b) const {
            return View(a) == b;
        }

        bool operator()(const eastl::string_view& b, const char* a) const {
            return View(a) == b;
        }
    };

    mutable std::mutex mutex_;
    StringArena arena_;
    eastl::hash_set<const char*, EntryHash, EntryEq> set_;
};

}  // namespace tryengine::core