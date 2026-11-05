#pragma once

#include <atomic>
#include "ResourceControlBlock.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/Inline.hpp"

namespace tryengine::resources {

template <typename T>
class ResourceHandle {
public:
    ResourceHandle() = default;

    explicit ResourceHandle(ResourceControlBlock<T>* control_block)
        : control_block_(control_block) {
        if (control_block_) {
            control_block_->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    ResourceHandle(const ResourceHandle& other) : control_block_(other.control_block_) {
        if (control_block_) {
            control_block_->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    ResourceHandle(ResourceHandle&& other) noexcept : control_block_(other.control_block_) {
        other.control_block_ = nullptr;
    }

    ResourceHandle& operator=(const ResourceHandle& other) {
        if (this != &other) {
            Reset();
            control_block_ = other.control_block_;
            if (control_block_) {
                control_block_->ref_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }

    ResourceHandle& operator=(ResourceHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            control_block_ = other.control_block_;
            other.control_block_ = nullptr;
        }
        return *this;
    }

    ~ResourceHandle() {
        Reset();
    }

    TRY_INLINE bool operator==(const ResourceHandle& other) const noexcept { return control_block_ == other.control_block_; }
    TRY_INLINE bool operator!=(const ResourceHandle& other) const noexcept { return control_block_ != other.control_block_; }

    TRY_INLINE bool IsReady() const noexcept {
        return control_block_ && control_block_->state.load(std::memory_order_acquire) == ResourceState::Ready;
    }

    TRY_INLINE bool IsLoading() const noexcept {
        return control_block_ && control_block_->state.load(std::memory_order_acquire) == ResourceState::Loading;
    }

    TRY_INLINE ResourceState GetState() const noexcept {
        return control_block_ ? control_block_->state.load(std::memory_order_acquire) : ResourceState::Empty;
    }

    TRY_INLINE explicit operator bool() const noexcept { return IsReady(); }

    TRY_INLINE T& operator*() const {
        TRY_ASSERT(IsReady(), "Attempted to dereference a resource that is not ready!");
        return *(control_block_->data);
    }

    TRY_INLINE T* operator->() const {
        TRY_ASSERT(IsReady(), "Attempted to access a resource that is not ready!");
        return control_block_->data;
    }

    TRY_INLINE T* Get() const noexcept { return IsReady() ? control_block_->data : nullptr; }

    void Reset() noexcept {
        if (control_block_) {
            control_block_->ref_count.fetch_sub(1, std::memory_order_acq_rel);
            control_block_ = nullptr;
        }
    }

    TRY_INLINE uint32_t RefCount() const noexcept {
        return control_block_ ? control_block_->ref_count.load(std::memory_order_relaxed) : 0;
    }

    ResourceControlBlock<T>* GetControlBlock() const noexcept { return control_block_; }

private:
    ResourceControlBlock<T>* control_block_{nullptr};
};

}  // namespace tryengine::resources