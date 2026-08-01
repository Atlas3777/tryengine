// #include "editor/FileWatcher.hpp"
//
// #include <filesystem>
// #include <utility>
// #include <vector>
//
// namespace fs = std::filesystem;
//
// namespace tryeditor {
//
// FileWatcher::FileWatcher(tryengine::filesystem::NativePath root,
//                           tryengine::async::MainThreadExecutor& main_thread,
//                           std::chrono::milliseconds poll_interval)
//     : root_(std::move(root)), main_thread_(main_thread), poll_interval_(poll_interval) {}
//
// FileWatcher::~FileWatcher() {
//     Stop();
// }
//
// void FileWatcher::SetReimportCallback(ReimportCallback callback) {
//     std::lock_guard lock(callback_mutex_);
//     reimport_callback_ = std::move(callback);
// }
//
// void FileWatcher::Start() {
//     if (running_.exchange(true)) {
//         return; // уже запущен
//     }
//     stop_requested_ = false;
//     worker_ = std::thread([this] { WorkerLoop(); });
// }
//
// void FileWatcher::Stop() {
//     if (!running_.exchange(false)) {
//         return; // уже остановлен
//     }
//     {
//         std::lock_guard lock(cv_mutex_);
//         stop_requested_ = true;
//     }
//     cv_.notify_all();
//     if (worker_.joinable()) {
//         worker_.join();
//     }
// }
//
// void FileWatcher::WorkerLoop() {
//     // Первый проход только наполняет known_files_ и не репортит события —
//     // иначе на старте вотчер "обнаружит" все существующие ассеты как новые
//     // и реимпортирует весь проект разом.
//     ScanOnce(/*report_changes=*/false);
//
//     std::unique_lock lock(cv_mutex_);
//     while (!stop_requested_) {
//         cv_.wait_for(lock, poll_interval_, [this] { return stop_requested_; });
//         if (stop_requested_) break;
//
//         lock.unlock();
//         ScanOnce(/*report_changes=*/true);
//         lock.lock();
//     }
// }
//
// void FileWatcher::ScanOnce(bool report_changes) {
//     std::string root_str = root_.ToNativeSeparators('/');
//
//     std::error_code ec;
//     if (!fs::exists(root_str, ec) || ec) {
//         // Директория пока недоступна (например, ещё не примонтирована/не
//         // создана) — просто попробуем на следующем проходе.
//         return;
//     }
//
//     std::unordered_map<std::string, FileState> current;
//     current.reserve(known_files_.size());
//
//     fs::recursive_directory_iterator it(root_str, fs::directory_options::skip_permission_denied, ec);
//     fs::recursive_directory_iterator end;
//     for (; !ec && it != end; it.increment(ec)) {
//         const auto& entry = *it;
//
//         std::error_code local_ec;
//         if (entry.is_directory(local_ec) || local_ec) {
//             continue;
//         }
//
//         std::string path_str = entry.path().generic_string();
//         if (!IsWatchedAsset(path_str)) {
//             continue;
//         }
//
//         FileState state;
//         state.size = static_cast<std::int64_t>(entry.file_size(local_ec));
//         if (local_ec) continue;
//
//         auto mtime = entry.last_write_time(local_ec);
//         if (local_ec) continue;
//         state.mtime_ns = mtime.time_since_epoch().count();
//
//         current.emplace(std::move(path_str), state);
//     }
//
//     if (!report_changes) {
//         known_files_ = std::move(current);
//         return;
//     }
//
//     std::vector<FileChangeEvent> events;
//
//     for (const auto& [path_str, state] : current) {
//         auto found = known_files_.find(path_str);
//         if (found == known_files_.end()) {
//             events.push_back({tryengine::filesystem::NativePath(path_str), FileChangeKind::Created});
//         } else if (found->second.size != state.size || found->second.mtime_ns != state.mtime_ns) {
//             events.push_back({tryengine::filesystem::NativePath(path_str), FileChangeKind::Modified});
//         }
//     }
//
//     for (const auto& [path_str, state] : known_files_) {
//         if (current.find(path_str) == current.end()) {
//             events.push_back({tryengine::filesystem::NativePath(path_str), FileChangeKind::Removed});
//         }
//     }
//
//     known_files_ = std::move(current);
//
//     if (events.empty()) {
//         return;
//     }
//
//     ReimportCallback callback;
//     {
//         std::lock_guard lock(callback_mutex_);
//         callback = reimport_callback_;
//     }
//     if (!callback) {
//         return;
//     }
//
//     // Реимпорт зовём на главном потоке — он обычно трогает состояние
//     // движка/GPU, а MainThreadExecutor и так дренится каждый кадр в
//     // EditorApp::Run().
//     main_thread_.Post([callback = std::move(callback), events = std::move(events)] {
//         for (const auto& event : events) {
//             callback(event);
//         }
//     });
// }
//
// bool FileWatcher::IsWatchedAsset(const std::string& native_path) {
//     // Заглушка-фильтр: игнорируем скрытые и временные файлы. Если нужно
//     // watch'ить только конкретные расширения (*.glb, *.mat, *.scene, *.meta
//     // и т.п.) — фильтр по расширению добавляется здесь.
//     fs::path p(native_path);
//     const std::string filename = p.filename().string();
//
//     if (filename.empty() || filename.front() == '.') {
//         return false;
//     }
//     if (filename.size() >= 4 && filename.compare(filename.size() - 4, 4, ".tmp") == 0) {
//         return false;
//     }
//     if (filename.back() == '~') {
//         return false;
//     }
//
//     return true;
// }
//
// } // namespace tryeditor
