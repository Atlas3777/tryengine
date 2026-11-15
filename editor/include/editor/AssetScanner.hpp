#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstdint>

namespace tryeditor {

// Пара "ассет + .meta". Строки здесь пока eastl::string, потому что уходят в корутины
// и в AsyncFileManager (который принимает eastl::string). Уйдут вместе с ним.
struct DiscoveredAssetPair {
    eastl::string asset_path;  // "game/assets/textures/hero.png"
    eastl::string meta_path;   // "game/assets/textures/hero.png.meta"
};

// Одна запись структуры диска. Путь лежит в ScanResult::blob (см. ScanResult::Path).
struct ScanEntry {
    uint32_t offset;
    uint32_t length;
    bool is_dir;
};

struct ScanResult {
    eastl::vector<DiscoveredAssetPair> matched_assets;  // валидные пары
    eastl::vector<eastl::string> orphan_assets;         // ассеты без .meta
    eastl::vector<eastl::string> orphan_metas;          // .meta без ассета

    // Структура диска — плоский список в порядке обхода: родитель ВСЕГДА раньше ребёнка,
    // entries[0] — корень. В нём только папки и ассеты (.meta не входят).
    // Все пути одним буфером: ни одной аллокации на путь. Родителя и имя не храним —
    // они выводятся из самого пути.
    eastl::vector<ScanEntry> entries;
    eastl::vector<char> blob;

    eastl::string_view Path(const ScanEntry& e) const {
        return eastl::string_view(blob.data() + e.offset, e.length);
    }

    // Кладёт строку в blob (без записи в entries). Возвращает смещение.
    uint32_t Store(eastl::string_view s) {
        const uint32_t offset = static_cast<uint32_t>(blob.size());
        blob.insert(blob.end(), s.data(), s.data() + s.size());
        return offset;
    }

    void AddEntry(eastl::string_view path, bool is_dir) {
        entries.push_back(ScanEntry{Store(path), static_cast<uint32_t>(path.size()), is_dir});
    }
};

class AssetScanner {
public:
    // Рекурсивно сканирует root_dir. Пути в результате — с '/', относительно того же, что и root_dir.
    static ScanResult ScanDirectory(eastl::string_view root_dir);
};

}  // namespace tryeditor