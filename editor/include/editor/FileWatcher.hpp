#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <functional>

#include "engine/async/MainThreadExecutor.hpp"

namespace tryeditor {

enum class FileChangeKind {
    Created,
    Modified,
    Removed,
    Renamed
};

struct FileChangeEvent {
    eastl::string path;
    FileChangeKind kind;
};

using ReimportCallback = std::function<void(const FileChangeEvent&)>;

class FileWatcher {
public:
    eastl::vector<FileChangeEvent> file_watcher_events;  // тоже заглушка
    void WatchDirrectory(eastl::string path) {
        //fanotify
    }
    // [[nodiscard]] bool IsRunning() const noexcept { return running_.load(std::memory_order_relaxed); }

private:

};

} // namespace tryeditor
