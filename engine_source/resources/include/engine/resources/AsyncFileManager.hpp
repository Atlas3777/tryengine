#pragma once

#include <EASTL/fixed_string.h>
#include <EASTL/hash_set.h>
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <coroutine>
#include <fcntl.h>
#include <liburing.h>
#include <mutex>
#include <sys/stat.h>
#include <tuple>
#include <utility>

#include "engine/async/Executor.hpp"

namespace tryengine::resources {
enum class TaskStatus : uint8_t { Unused, Pending, Completed, Failed };

struct FileTime {
    int64_t sec{0};
    uint64_t nsec{0};
};

inline bool operator>(const FileTime& a, const FileTime& b) noexcept {
    if (a.sec != b.sec)
        return a.sec > b.sec;
    return a.nsec > b.nsec;
}

inline bool operator<(const FileTime& a, const FileTime& b) noexcept {
    return b > a;
}

inline bool operator<=(const FileTime& a, const FileTime& b) noexcept {
    return !(a > b);
}

struct FileTask {
    TaskStatus status = TaskStatus::Unused;
    eastl::vector<uint8_t> storage;
    int bytes_read = -1;
    uint32_t slot_index = 0;

    eastl::string_view path;

    std::coroutine_handle<> continuation = nullptr;
    uint32_t* wait_counter = nullptr;
    Executor* executor = nullptr;
};

class FileHandle {
public:
    FileHandle() = default;
    explicit FileHandle(FileTask* task) noexcept : task_(task) {}

    ~FileHandle() { Release(); }

    FileHandle(FileHandle&& other) noexcept : task_(std::exchange(other.task_, nullptr)) {}

    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            Release();
            task_ = std::exchange(other.task_, nullptr);
        }
        return *this;
    }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    [[nodiscard]] bool IsValid() const noexcept { return task_ != nullptr; }
    [[nodiscard]] bool IsReady() const noexcept { return task_ && task_->status == TaskStatus::Completed; }
    [[nodiscard]] bool IsFailed() const noexcept { return task_ && task_->status == TaskStatus::Failed; }
    [[nodiscard]] bool IsPending() const noexcept { return task_ && task_->status == TaskStatus::Pending; }

    [[nodiscard]] int BytesRead() const noexcept { return task_ ? task_->bytes_read : -1; }
    [[nodiscard]] int BytesWritten() const noexcept { return BytesRead(); }

    [[nodiscard]] eastl::span<const uint8_t> GetData() const noexcept {
        if (IsReady() && task_) {
            return {task_->storage.data(), task_->storage.size()};
        }
        TRY_ASSERT(false,"IsReady == false");
        return {};
    }

    [[nodiscard]] eastl::vector<uint8_t> TakeData() noexcept {
        if (IsReady() && task_) {
            return std::move(task_->storage);
        }
        TRY_ASSERT(false,"IsReady == false");
        return {};
    }

    [[nodiscard]] eastl::string_view GetPath() const noexcept { return task_ ? task_->path : eastl::string_view{}; }

    [[nodiscard]] FileTime GetMtime() const noexcept {
        if (IsReady() && task_ && task_->storage.size() >= sizeof(struct statx)) {
            const auto* st = reinterpret_cast<const struct statx*>(task_->storage.data());
            return {st->stx_mtime.tv_sec, static_cast<uint64_t>(st->stx_mtime.tv_nsec)};
        }
        return {};
    }

    [[nodiscard]] uint64_t GetFileSize() const noexcept {
        if (IsReady() && task_ && task_->storage.size() >= sizeof(struct statx)) {
            const auto* st = reinterpret_cast<const struct statx*>(task_->storage.data());
            return st->stx_size;
        }
        return 0;
    }

    void Release() noexcept;  // определён ниже AsyncFileManager, т.к. чистит owned_paths_

private:
    friend class AsyncFileManager;
    friend struct FileIoAwaiter;
    template <typename...>
    friend struct GroupIoAwaiter;
    friend struct VectorIoAwaiter;

    [[nodiscard]] uint32_t TaskIndex() const noexcept;

    FileTask* task_{nullptr};
};

inline void FileHandle::Release() noexcept {
    LogInfo("FileHandle Release");
    if (task_) {
        TRY_ASSERT(task_->status != TaskStatus::Pending, "Releasing FileHandle while task is still Pending!");
        task_->status = TaskStatus::Unused;
        task_->storage.clear();
        task_->path = eastl::string_view{};
        task_->continuation = nullptr;
        task_->wait_counter = nullptr;
        task_ = nullptr;
    }
}

struct FileIoAwaiter {
    const FileHandle& handle;

    bool await_ready() const noexcept { return !handle.IsPending(); }

    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
        handle.task_->continuation = awaiting;
        handle.task_->wait_counter = nullptr;
        handle.task_->executor = async::GetCurrentExecutor();
    }

    void await_resume() const noexcept {}
};

inline FileIoAwaiter operator co_await(const FileHandle& handle) noexcept {
    return FileIoAwaiter{handle};
}

template <typename... Handles>
struct GroupIoAwaiter {
    std::tuple<const Handles&...> handles;
    uint32_t remaining_ = 0;

    bool await_ready() noexcept {
        std::apply([this](const auto&... h) { ((void) (h.IsPending() && ++remaining_), ...); }, handles);
        return remaining_ == 0;
    }

    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
        std::apply(
            [this, awaiting](const auto&... h) {
                auto register_one = [this, awaiting](const auto& handle) {
                    if (handle.IsPending() && handle.task_) {
                        handle.task_->continuation = awaiting;
                        handle.task_->wait_counter = &remaining_;
                        handle.task_->executor = async::GetCurrentExecutor();
                    }
                };
                (register_one(h), ...);
            },
            handles);
    }

    void await_resume() const noexcept {}
};

template <typename... Handles>
    requires(std::same_as<std::decay_t<Handles>, FileHandle> && ...)
inline GroupIoAwaiter<Handles...> WaitAllFiles(const Handles&... handles) noexcept {
    return GroupIoAwaiter<Handles...>{std::tie(handles...)};
}

struct VectorIoAwaiter {
    eastl::span<const FileHandle> handles;
    uint32_t remaining{0};

    bool await_ready() noexcept {
        for (const auto& h : handles) {
            if (h.IsPending())
                ++remaining;
        }
        return remaining == 0;
    }

    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
        for (const auto& h : handles) {
            if (h.IsPending() && h.task_) {
                h.task_->continuation = awaiting;
                h.task_->wait_counter = &remaining;
                h.task_->executor = async::GetCurrentExecutor();
            }
        }
    }

    void await_resume() const noexcept {}
};

inline VectorIoAwaiter WaitAllFiles(eastl::span<const FileHandle> handles) noexcept {
    return VectorIoAwaiter{handles};
}

class AsyncFileManager {
public:
    static constexpr uint32_t MAX_CONCURRENT_TASKS = 512;

    explicit AsyncFileManager() {
        io_uring_queue_init(MAX_CONCURRENT_TASKS, &ring_, 0);
        io_uring_register_files_sparse(&ring_, MAX_CONCURRENT_TASKS);

        free_fd_slots_.reserve(MAX_CONCURRENT_TASKS);
        for (uint32_t i = 0; i < MAX_CONCURRENT_TASKS; ++i) {
            free_fd_slots_.push_back(i);
        }
    }

    ~AsyncFileManager() {
        io_uring_unregister_files(&ring_);
        io_uring_queue_exit(&ring_);
    }

    AsyncFileManager(const AsyncFileManager&) = delete;
    AsyncFileManager& operator=(const AsyncFileManager&) = delete;

    FileHandle ReadChunkAsync(eastl::string_view path, uint64_t offset, uint32_t size) {
        return ReadChunkAsyncImpl(path, offset, size, PathOwnership::Borrowed);
    }

    FileHandle ReadChunkAsyncCopy(eastl::string_view path, uint64_t offset, uint32_t size) {
        return ReadChunkAsyncImpl(path, offset, size, PathOwnership::Owned);
    }

    FileHandle GetStatAsync(eastl::string_view path) { return GetStatAsyncImpl(path, PathOwnership::Borrowed); }

    FileHandle GetStatAsyncCopy(eastl::string_view path) { return GetStatAsyncImpl(path, PathOwnership::Owned); }

    FileHandle WriteChunkAsync(eastl::string_view path, eastl::span<const uint8_t> data, uint64_t offset = 0) {
        return WriteChunkAsyncImpl(path, data, offset, PathOwnership::Borrowed);
    }

    FileHandle WriteChunkAsyncCopy(eastl::string_view path, eastl::span<const uint8_t> data, uint64_t offset = 0) {
        return WriteChunkAsyncImpl(path, data, offset, PathOwnership::Owned);
    }

    uint32_t Submit() {
        std::lock_guard lock(ring_mutex_);
        if (has_unsubmitted_sqes_) {
            int res = io_uring_submit(&ring_);
            has_unsubmitted_sqes_ = false;
            return res > 0 ? static_cast<uint32_t>(res) : 0;
        }
        return 0;
    }

    void Pull() {
        io_uring_cqe* cqe = nullptr;
        while (io_uring_peek_cqe(&ring_, &cqe) == 0) {
            uint64_t data = io_uring_cqe_get_data64(cqe);

            if (data & OPEN_FAIL_TAG) {
                io_uring_cqe_seen(&ring_, cqe);
                continue;
            }

            if (data & CLOSE_TAG) {
                const uint32_t fd_slot = static_cast<uint32_t>(data & ~CLOSE_TAG);
                {
                    std::lock_guard lock(ring_mutex_);
                    free_fd_slots_.push_back(fd_slot);
                }
                io_uring_cqe_seen(&ring_, cqe);
                continue;
            }

            const uint64_t task_idx = data;
            if (task_idx < MAX_CONCURRENT_TASKS) {
                FileTask& task = tasks_[task_idx];

                if (cqe->res >= 0) {
                    task.status = TaskStatus::Completed;
                    task.bytes_read = cqe->res;
                } else {
                    task.status = TaskStatus::Failed;
                }

                if (task.continuation) {
                    bool should_resume = !task.wait_counter || (--(*task.wait_counter) == 0);
                    auto handle = task.continuation;
                    auto* exec = task.executor;

                    task.continuation = nullptr;
                    task.wait_counter = nullptr;
                    task.executor = nullptr;

                    if (should_resume) {
                        if (exec) {
                            // TRY_LOG_INFO("vy");
                            exec->Schedule(handle);
                        } else {
                            TRY_ASSERT(exec, "Executor == nullptr");
                            handle.resume();
                        }
                    }
                }
            }

            io_uring_cqe_seen(&ring_, cqe);
        }
    }

    void ReleaseTask(uint32_t task_idx) noexcept {
        FileTask& task = tasks_[task_idx];
        TRY_ASSERT(task.status != TaskStatus::Pending, "Releasing a pending task!");
        task.status = TaskStatus::Unused;
        task.storage.clear();
        task.path = eastl::string_view{};
        owned_paths_[task_idx].clear();
        task.continuation = nullptr;
        task.wait_counter = nullptr;
    }

private:
    enum class PathOwnership : uint8_t { Borrowed, Owned };

    static constexpr uint64_t OPEN_FAIL_TAG = 1ull << 62;
    static constexpr uint64_t CLOSE_TAG = 1ull << 63;

    void init_task(FileTask& task, uint32_t task_idx) noexcept {
        task.status = TaskStatus::Pending;
        task.bytes_read = -1;
        task.continuation = nullptr;
        task.wait_counter = nullptr;
        task.storage.clear();
        task.path = eastl::string_view{};
        owned_paths_[task_idx].clear();
    }

    void assign_path(FileTask& task, uint32_t task_idx, eastl::string_view path, PathOwnership ownership) noexcept {
        if (ownership == PathOwnership::Owned) {
            auto& owned = owned_paths_[task_idx];
            owned.assign(path.data(), path.size());
            task.path = eastl::string_view(owned.data(), owned.size());
        } else {
            task.path = path;
        }
    }

    uint32_t allocate_task_slot() noexcept {
        for (uint32_t i = 0; i < MAX_CONCURRENT_TASKS; ++i) {
            if (tasks_[i].status == TaskStatus::Unused) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    FileHandle ReadChunkAsyncImpl(eastl::string_view path, uint64_t offset, uint32_t size, PathOwnership ownership) {
        std::lock_guard lock(ring_mutex_);

        uint32_t task_idx = allocate_task_slot();
        TRY_ASSERT(task_idx != UINT32_MAX, "AsyncFileManager: MAX_CONCURRENT_TASKS limit exceeded!");
        TRY_ASSERT(!free_fd_slots_.empty(), "AsyncFileManager: Out of free FD slots!");
        if (task_idx == UINT32_MAX || free_fd_slots_.empty()) {
            return FileHandle{};
        }

        FileTask& task = tasks_[task_idx];
        init_task(task, task_idx);
        task.storage.resize(size);
        assign_path(task, task_idx, path, ownership);

        uint32_t fd_slot = free_fd_slots_.back();
        free_fd_slots_.pop_back();
        task.slot_index = fd_slot;

        io_uring_sqe* sqe_open = io_uring_get_sqe(&ring_);
        io_uring_sqe* sqe_read = io_uring_get_sqe(&ring_);
        io_uring_sqe* sqe_close = io_uring_get_sqe(&ring_);

        TRY_ASSERT(sqe_open && sqe_read && sqe_close, "AsyncFileManager: Ring SQE allocation failed!");
        if (!sqe_open || !sqe_read || !sqe_close) {
            free_fd_slots_.push_back(fd_slot);
            task.status = TaskStatus::Failed;
            return FileHandle{&task};
        }

        io_uring_prep_openat_direct(sqe_open, AT_FDCWD, task.path.data(), O_RDONLY, 0, fd_slot);
        sqe_open->flags |= IOSQE_IO_LINK | IOSQE_CQE_SKIP_SUCCESS;
        io_uring_sqe_set_data64(sqe_open, OPEN_FAIL_TAG | task_idx);

        io_uring_prep_read(sqe_read, fd_slot, task.storage.data(), size, offset);
        sqe_read->flags |= IOSQE_FIXED_FILE | IOSQE_IO_HARDLINK;
        io_uring_sqe_set_data64(sqe_read, task_idx);

        io_uring_prep_close_direct(sqe_close, fd_slot);
        io_uring_sqe_set_data64(sqe_close, CLOSE_TAG | fd_slot);

        has_unsubmitted_sqes_ = true;
        return FileHandle{&task};
    }

    FileHandle GetStatAsyncImpl(eastl::string_view path, PathOwnership ownership) {
        std::lock_guard lock(ring_mutex_);

        uint32_t task_idx = allocate_task_slot();
        TRY_ASSERT(task_idx != UINT32_MAX, "AsyncFileManager: MAX_CONCURRENT_TASKS limit exceeded!");
        if (task_idx == UINT32_MAX) {
            return FileHandle{};
        }

        FileTask& task = tasks_[task_idx];
        init_task(task, task_idx);
        task.storage.resize(sizeof(struct statx));
        assign_path(task, task_idx, path, ownership);

        io_uring_sqe* sqe_stat = io_uring_get_sqe(&ring_);
        TRY_ASSERT(sqe_stat, "AsyncFileManager: Ring SQE allocation failed!");
        if (!sqe_stat) {
            task.status = TaskStatus::Failed;
            return FileHandle{&task};
        }

        io_uring_prep_statx(sqe_stat, AT_FDCWD, task.path.data(), 0, STATX_BASIC_STATS,
                            reinterpret_cast<struct statx*>(task.storage.data()));
        io_uring_sqe_set_data64(sqe_stat, task_idx);

        has_unsubmitted_sqes_ = true;
        return FileHandle{&task};
    }

    FileHandle WriteChunkAsyncImpl(eastl::string_view path, eastl::span<const uint8_t> data, uint64_t offset,
                                   PathOwnership ownership) {
        EnsureDirectoriesExist(GetParentPath(path));

        std::lock_guard lock(ring_mutex_);

        uint32_t task_idx = allocate_task_slot();
        TRY_ASSERT(task_idx != UINT32_MAX, "AsyncFileManager: MAX_CONCURRENT_TASKS limit exceeded!");
        TRY_ASSERT(!free_fd_slots_.empty(), "AsyncFileManager: Out of free FD slots!");
        if (task_idx == UINT32_MAX || free_fd_slots_.empty()) {
            return FileHandle{};
        }

        FileTask& task = tasks_[task_idx];
        init_task(task, task_idx);
        assign_path(task, task_idx, path, ownership);

        const uint8_t* write_ptr = data.data();

        if (ownership == PathOwnership::Owned) {
            task.storage.assign(data.begin(), data.end());
            write_ptr = task.storage.data();
        }

        uint32_t fd_slot = free_fd_slots_.back();
        free_fd_slots_.pop_back();
        task.slot_index = fd_slot;

        io_uring_sqe* sqe_open = io_uring_get_sqe(&ring_);
        io_uring_sqe* sqe_write = io_uring_get_sqe(&ring_);
        io_uring_sqe* sqe_close = io_uring_get_sqe(&ring_);

        TRY_ASSERT(sqe_open && sqe_write && sqe_close, "AsyncFileManager: Ring SQE allocation failed!");
        if (!sqe_open || !sqe_write || !sqe_close) {
            free_fd_slots_.push_back(fd_slot);
            task.status = TaskStatus::Failed;
            return FileHandle{&task};
        }

        constexpr int open_flags = O_WRONLY | O_CREAT | O_TRUNC;
        constexpr mode_t mode = 0644;

        io_uring_prep_openat_direct(sqe_open, AT_FDCWD, task.path.data(), open_flags, mode, fd_slot);
        sqe_open->flags |= IOSQE_IO_LINK | IOSQE_CQE_SKIP_SUCCESS;
        io_uring_sqe_set_data64(sqe_open, OPEN_FAIL_TAG | task_idx);

        io_uring_prep_write(sqe_write, fd_slot, write_ptr, static_cast<unsigned int>(data.size()), offset);
        sqe_write->flags |= IOSQE_FIXED_FILE | IOSQE_IO_HARDLINK;
        io_uring_sqe_set_data64(sqe_write, task_idx);

        io_uring_prep_close_direct(sqe_close, fd_slot);
        io_uring_sqe_set_data64(sqe_close, CLOSE_TAG | fd_slot);

        has_unsubmitted_sqes_ = true;
        return FileHandle{&task};
    }

    static eastl::string_view GetParentPath(eastl::string_view path) noexcept {
        size_t last_slash = path.find_last_of("/\\");
        if (last_slash == eastl::string_view::npos) {
            return {};
        }
        return path.substr(0, last_slash);
    }

    void EnsureDirectoriesExist(eastl::string_view dir_path) {
        if (dir_path.empty())
            return;

        char buffer[512];
        if (dir_path.size() >= sizeof(buffer))
            return;

        eastl::copy(dir_path.begin(), dir_path.end(), buffer);
        buffer[dir_path.size()] = '\0';

        std::lock_guard lock(dirs_mutex_);

        for (char* p = buffer + 1; *p; ++p) {
            if (*p == '/' || *p == '\\') {
                const char save = *p;
                *p = '\0';
                mkdir_if_new(buffer, static_cast<size_t>(p - buffer));
                *p = save;
            }
        }

        mkdir_if_new(buffer, dir_path.size());
    }

    void mkdir_if_new(const char* buffer, size_t len) {
        eastl::string_view segment(buffer, len);
        if (known_dirs_.find(eastl::string(segment.data(), segment.size())) != known_dirs_.end()) {
            return;
        }

        const int res = ::mkdir(buffer, 0755);
        if (res == 0 || errno == EEXIST) {
            known_dirs_.emplace(segment.data(), segment.size());
        }
    }

    std::mutex ring_mutex_;

    io_uring ring_{};
    bool has_unsubmitted_sqes_{false};
    FileTask tasks_[MAX_CONCURRENT_TASKS];

    eastl::fixed_string<char, 256> owned_paths_[MAX_CONCURRENT_TASKS];

    eastl::vector<uint32_t> free_fd_slots_;

    std::mutex dirs_mutex_;
    eastl::hash_set<eastl::string> known_dirs_;
};

}  // namespace tryengine::resources