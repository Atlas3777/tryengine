#pragma once

#include <EASTL/vector.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>

namespace tryengine::core {

// Аллокатор мелких блоков с освобождением. Чанки по 64 КБ не двигаются => указатели стабильны.
// Классы размеров 16..2048 (степени двойки), free-list на класс. Alloc/Free — O(1), без malloc.
// Блоки > 2048 байт идут в operator new. НЕ потокобезопасен (StringTable берёт мьютекс).
class StringArena {
public:
    StringArena() = default;
    StringArena(const StringArena&) = delete;
    StringArena& operator=(const StringArena&) = delete;

    ~StringArena() {
        for (void* c : chunks_)
            ::operator delete(c);
    }

    // Возвращает память, выровненную по 16.
    void* Alloc(uint32_t size) {
        if (size > kMaxClassSize)
            return ::operator new(size);
        const uint32_t c = ClassOf(size);
        if (FreeNode* n = free_[c]) {
            free_[c] = n->next;
            return n;
        }
        return Carve(ClassSize(c));
    }

    // size — тот же, что передавали в Alloc.
    void Free(void* p, uint32_t size) {
        if (size > kMaxClassSize) {
            ::operator delete(p);
            return;
        }
        const uint32_t c = ClassOf(size);
        auto* n = static_cast<FreeNode*>(p);
        n->next = free_[c];
        free_[c] = n;
    }

private:
    static constexpr uint32_t kMinShift = 4;  // 16 байт
    static constexpr uint32_t kClasses = 8;   // 16..2048
    static constexpr uint32_t kMaxClassSize = 16u << (kClasses - 1);
    static constexpr size_t kChunkSize = 64 * 1024;

    struct FreeNode {
        FreeNode* next;
    };

    static uint32_t ClassOf(uint32_t s) {
        return s <= 16 ? 0u : static_cast<uint32_t>(std::bit_width(s - 1)) - kMinShift;
    }

    static uint32_t ClassSize(uint32_t c) {
        return 16u << c;
    }

    void* Carve(uint32_t sz) {
        if (cur_ == nullptr || cur_ + sz > end_) {
            auto* chunk = static_cast<std::byte*>(::operator new(kChunkSize));
            chunks_.push_back(chunk);
            cur_ = chunk;
            end_ = chunk + kChunkSize;
        }
        void* r = cur_;
        cur_ += sz;
        return r;
    }

    FreeNode* free_[kClasses] = {};
    std::byte* cur_ = nullptr;
    std::byte* end_ = nullptr;
    eastl::vector<void*> chunks_;
};

}  // namespace tryengine::core