#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/RunAndForget.hpp"
#include "engine/async/Task.hpp"
#include "engine/resources/AssetBytes.hpp"

namespace tryengine::resources {

// Разовое асинхронное чтение текста. В отличие от типизированных кешей
// `ResourceManager` здесь ничего не остаётся после `Take`: документ разбирается
// скриптовым слоем один раз и больше отсюда не читается, поэтому кешировать текст
// нечего. Handle — это запрос, а не id ассета: два одновременных читателя одного
// файла получают два независимых чтения и не могут освободить текст друг друга.
class TextRequests {
public:
    TextRequests(AsyncFileManager& file_manager, AssetRegistry& registry) : state_(std::make_shared<Shared>()) {
        state_->file_manager = &file_manager;
        state_->registry = &registry;
    }

    TextRequests(const TextRequests&) = delete;
    TextRequests& operator=(const TextRequests&) = delete;

    //! Начинает одно чтение. Возвращает 0, если id неизвестен реестру.
    uint64_t Request(uint64_t asset_id) {
        if (asset_id == 0 || !state_->registry->GetAssetLocation(asset_id).has_value())
            return 0;

        uint64_t handle = 0;
        {
            std::lock_guard lock(state_->mutex);
            handle = state_->next_handle++;
            state_->entries[handle] = std::make_unique<Entry>();
        }

        async::RunAndForget(async::ThreadPool(), Load(state_, handle, asset_id));
        return handle;
    }

    [[nodiscard]] bool IsReady(uint64_t handle) const {
        return StateOf(handle) == EntryState::Ready;
    }

    [[nodiscard]] bool IsFailed(uint64_t handle) const {
        return StateOf(handle) == EntryState::Failed;
    }

    //! Забирает текст и освобождает запрос. "" — для неизвестного или незавершённого.
    eastl::string Take(uint64_t handle) {
        std::lock_guard lock(state_->mutex);
        auto it = state_->entries.find(handle);
        if (it == state_->entries.end())
            return {};

        eastl::string text;
        if (it->second->state.load(std::memory_order_acquire) == EntryState::Ready)
            text = std::move(it->second->text);
        state_->entries.erase(it);
        return text;
    }

    //! Освобождает запрос, текст которого не будет прочитан (abort, ошибка).
    void Cancel(uint64_t handle) {
        std::lock_guard lock(state_->mutex);
        state_->entries.erase(handle);
    }

private:
    enum class EntryState : uint8_t { Unknown, Loading, Ready, Failed };

    struct Entry {
        std::atomic<EntryState> state{EntryState::Loading};
        eastl::string text;
    };

    struct Shared {
        AsyncFileManager* file_manager{nullptr};
        AssetRegistry* registry{nullptr};

        std::mutex mutex;
        eastl::hash_map<uint64_t, std::unique_ptr<Entry>> entries;
        uint64_t next_handle{1};
    };

    [[nodiscard]] EntryState StateOf(uint64_t handle) const {
        std::lock_guard lock(state_->mutex);
        auto it = state_->entries.find(handle);
        if (it == state_->entries.end())
            return EntryState::Unknown;
        return it->second->state.load(std::memory_order_acquire);
    }

    // Чтение живёт на пуле потоков и переживает `ResourceManager`: состояние держится
    // общим указателем, а запись в него идёт под тем же мьютексом, что и `Take`.
    static async::Task<void> Load(std::shared_ptr<Shared> shared, uint64_t handle, uint64_t asset_id) {
        auto text = co_await ReadAssetText(*shared->file_manager, *shared->registry, asset_id);

        std::lock_guard lock(shared->mutex);
        auto it = shared->entries.find(handle);
        if (it == shared->entries.end())
            co_return {};  // запрос отменён, пока чтение было в полёте

        if (!text.has_value()) {
            it->second->state.store(EntryState::Failed, std::memory_order_release);
            co_return {};
        }

        it->second->text = std::move(*text);
        it->second->state.store(EntryState::Ready, std::memory_order_release);
        co_return {};
    }

    std::shared_ptr<Shared> state_;
};

}  // namespace tryengine::resources
