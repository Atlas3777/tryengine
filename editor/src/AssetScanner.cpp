#include "editor/AssetScanner.hpp"
#include "engine/platform/Directory.hpp"

#include <EASTL/sort.h>

namespace tryeditor {

namespace {

constexpr eastl::string_view kMetaExt = ".meta";

struct FileRef {
    uint32_t offset;
    uint32_t length;
    bool is_meta;
    bool matched;
};

// Файлы одной папки. Пару ассет+.meta можно найти только внутри одной папки,
// поэтому на каждую открытую папку — свой кадр; никакой глобальной "комнаты ожидания".
struct Frame {
    eastl::vector<FileRef> files;
};

class ScanVisitor {
public:
    explicit ScanVisitor(ScanResult& result) : result_(result) { frames_.emplace_back(); }

    bool OnDirectoryEnter(eastl::string_view full, eastl::string_view) {
        result_.AddEntry(full, true);
        frames_.emplace_back();
        return true;
    }

    void OnDirectoryLeave() {
        Resolve(frames_.back());
        frames_.pop_back();
    }

    void OnFile(eastl::string_view full, eastl::string_view name) {
        const bool is_meta = name.size() >= kMetaExt.size() && name.substr(name.size() - kMetaExt.size()) == kMetaExt;
        const uint32_t offset = result_.Store(full);
        if (!is_meta)
            result_.entries.push_back(ScanEntry{offset, static_cast<uint32_t>(full.size()), false});
        frames_.back().files.push_back(FileRef{offset, static_cast<uint32_t>(full.size()), is_meta, false});
    }

    void Finish() { Resolve(frames_.front()); }

private:
    eastl::string_view View(const FileRef& f) const {
        return eastl::string_view(result_.blob.data() + f.offset, f.length);
    }

    void Resolve(Frame& frame) {
        auto& files = frame.files;
        // Все пути в кадре имеют общий префикс-папку, поэтому сортировка по полному пути = по имени.
        eastl::sort(files.begin(), files.end(),
                    [this](const FileRef& a, const FileRef& b) { return View(a) < View(b); });

        for (FileRef& meta : files) {
            if (!meta.is_meta)
                continue;
            const eastl::string_view want = View(meta).substr(0, meta.length - kMetaExt.size());

            // бинарный поиск ассета с таким именем
            size_t lo = 0, hi = files.size();
            while (lo < hi) {
                const size_t mid = (lo + hi) / 2;
                if (View(files[mid]) < want)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo == files.size() || files[lo].is_meta || View(files[lo]) != want)
                continue;

            FileRef& asset = files[lo];
            const eastl::string_view a = View(asset), m = View(meta);
            result_.matched_assets.push_back(
                DiscoveredAssetPair{eastl::string(a.data(), a.size()), eastl::string(m.data(), m.size())});
            asset.matched = meta.matched = true;
        }

        for (const FileRef& f : files) {
            if (f.matched)
                continue;
            const eastl::string_view v = View(f);
            (f.is_meta ? result_.orphan_metas : result_.orphan_assets).push_back(eastl::string(v.data(), v.size()));
        }
    }

    ScanResult& result_;
    eastl::vector<Frame> frames_;
};

}  // namespace

ScanResult AssetScanner::ScanDirectory(eastl::string_view root_dir) {
    ScanResult result;
    while (root_dir.size() > 1 && (root_dir.back() == '/' || root_dir.back() == '\\'))
        root_dir.remove_suffix(1);

    result.AddEntry(root_dir, true);  // entries[0] — корень

    ScanVisitor visitor(result);
    tryengine::filesystem::Walk(root_dir, visitor);
    visitor.Finish();
    return result;
}

}  // namespace tryeditor