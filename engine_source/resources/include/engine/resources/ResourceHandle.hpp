#pragma once

#include <atomic>
#include <memory>

#include "ResourceControlBlock.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/Inline.hpp"

namespace tryengine::resources {

template <typename T>
class ResourceHandle {
public:
    ResourceHandle() = default;

    // Конструктор для кэша/хранилища, которое создает этот блок
    explicit ResourceHandle(std::shared_ptr<ResourceControlBlock<T>> control_block)
        : control_block_(std::move(control_block)) {}

    // Копирование и перемещение (дефолтные, так как shared_ptr делает всё за нас)
    ResourceHandle(const ResourceHandle&) = default;
    ResourceHandle(ResourceHandle&&) noexcept = default;
    ResourceHandle& operator=(const ResourceHandle&) = default;
    ResourceHandle& operator=(ResourceHandle&&) noexcept = default;
    ~ResourceHandle() = default;

    // Сравнение хэндлов (указывают ли они на один и тот же асет)
    __forceinline bool operator==(const ResourceHandle& other) const noexcept { return control_block_ == other.control_block_; }
    __forceinline bool operator!=(const ResourceHandle& other) const noexcept { return control_block_ != other.control_block_; }

    // Состояние ресурса (потокобезопасное)
    __forceinline bool IsReady() const noexcept {
        return control_block_ && control_block_->state.load(std::memory_order_acquire) == ResourceState::Ready;
    }

    __forceinline bool IsLoading() const noexcept {
        return control_block_ && control_block_->state.load(std::memory_order_acquire) == ResourceState::Loading;
    }

    __forceinline ResourceState GetState() const noexcept {
        return control_block_ ? control_block_->state.load(std::memory_order_acquire) : ResourceState::Empty;
    }

    // Быстрая проверка на валидность через if (handle)
    __forceinline explicit operator bool() const noexcept { return IsReady(); }

    // Доступ к данным (строгий assert в дебаге, если ресурс еще не готов)
    __forceinline T& operator*() const {
        TRY_ASSERT(IsReady(), "Attempted to dereference a resource that is not ready!");
        return *(control_block_->data);
    }

    __forceinline T* operator->() const {
        TRY_ASSERT(IsReady(), "Attempted to access a resource that is not ready!");
        return control_block_->data.get();
    }

    // Безопасный сырой указатель (вернет nullptr, если загрузка не завершена)
    __forceinline T* Get() const noexcept { return IsReady() ? control_block_->data.get() : nullptr; }

    __forceinline void Reset() noexcept { control_block_.reset(); }

    // Метод для вашей будущей функции Purge() в кэше.
    // Позволяет узнать, держит ли этот асет кто-то еще в движке, кроме самого кэша.
    __forceinline long UseCount() const noexcept { return control_block_ ? control_block_.use_count() : 0; }

    // Внутренний доступ для кэша и лоадеров
    const std::shared_ptr<ResourceControlBlock<T>>& GetControlBlock() const noexcept { return control_block_; }

private:
    std::shared_ptr<ResourceControlBlock<T>> control_block_{nullptr};
};

}  // namespace tryengine::resources