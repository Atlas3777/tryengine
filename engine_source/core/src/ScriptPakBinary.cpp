#include "engine/core/ScriptPakBinary.hpp"

#include <cstring>

#include "engine/core/MakeError.hpp"

namespace tryengine::core {

namespace {

template <typename T>
void Append(eastl::vector<uint8_t>& out, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

template <typename T>
bool ReadAt(eastl::span<const uint8_t> b, uint64_t off, T& out) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (off > b.size() || b.size() - off < sizeof(T))
        return false;
    std::memcpy(&out, b.data() + off, sizeof(T));
    return true;
}

}  // namespace

uint32_t Fnv1a32(eastl::span<const uint8_t> bytes) {
    uint32_t h = 2166136261u;
    for (uint8_t c : bytes) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

eastl::vector<uint8_t> SerializeScriptPak(const ScriptPak& pak) {
    eastl::vector<uint8_t> blob;
    eastl::vector<ScriptPakFileEntry> table;
    table.reserve(pak.files.size());

    for (const auto& f : pak.files) {
        ScriptPakFileEntry e{};
        e.name_off = uint32_t(blob.size());
        e.name_size = uint32_t(f.name.size());
        blob.insert(blob.end(), reinterpret_cast<const uint8_t*>(f.name.data()),
                    reinterpret_cast<const uint8_t*>(f.name.data()) + f.name.size());

        e.data_off = uint32_t(blob.size());
        e.data_size = uint32_t(f.data.size());
        e.data_hash = Fnv1a32({f.data.data(), f.data.size()});
        blob.insert(blob.end(), f.data.begin(), f.data.end());
        table.push_back(e);
    }

    ScriptPakHeader h{};
    h.magic = kScriptPakMagic;
    h.version = kScriptPakVersion;
    h.file_count = uint32_t(pak.files.size());
    h.table_off = sizeof(ScriptPakHeader);
    h.blob_off = h.table_off + h.file_count * uint32_t(sizeof(ScriptPakFileEntry));
    h.total_size = h.blob_off + uint32_t(blob.size());
    h.entry_index = pak.entry_index;

    eastl::vector<uint8_t> out;
    out.reserve(h.total_size);
    Append(out, h);
    for (const auto& e : table)
        Append(out, e);
    out.insert(out.end(), blob.begin(), blob.end());
    return out;
}

Result<ScriptPak> DeserializeScriptPak(eastl::span<const uint8_t> b) {
    ScriptPakHeader h;
    if (!ReadAt(b, 0, h))
        return LogAndMakeError("ScriptPak: файл слишком мал");
    if (h.magic != kScriptPakMagic)
        return LogAndMakeError("ScriptPak: неверный magic");
    if (h.version != kScriptPakVersion)
        return LogAndMakeError("ScriptPak: неподдерживаемая версия {}", h.version);
    if (h.total_size != b.size())
        return LogAndMakeError("ScriptPak: размер не совпадает, файл повреждён");

    const uint64_t table_end = uint64_t(h.table_off) + uint64_t(h.file_count) * sizeof(ScriptPakFileEntry);
    if (h.table_off < sizeof(ScriptPakHeader) || table_end > h.blob_off || h.blob_off > b.size())
        return LogAndMakeError("ScriptPak: таблица файлов выходит за границы");
    if (h.file_count == 0 || h.entry_index >= h.file_count)
        return LogAndMakeError("ScriptPak: неверный entry_index");

    const uint64_t blob_size = b.size() - h.blob_off;
    const uint8_t* blob = b.data() + h.blob_off;

    ScriptPak pak;
    pak.entry_index = h.entry_index;
    pak.files.reserve(h.file_count);

    for (uint32_t i = 0; i < h.file_count; ++i) {
        ScriptPakFileEntry e;
        if (!ReadAt(b, uint64_t(h.table_off) + uint64_t(i) * sizeof(e), e))
            return LogAndMakeError("ScriptPak: чтение таблицы");
        if (uint64_t(e.name_off) + e.name_size > blob_size || uint64_t(e.data_off) + e.data_size > blob_size)
            return LogAndMakeError("ScriptPak: запись {} выходит за blob", i);

        const uint8_t* data = blob + e.data_off;
        if (Fnv1a32({data, e.data_size}) != e.data_hash)
            return LogAndMakeError("ScriptPak: хеш не совпал для записи {}", i);

        ScriptPakFile f;
        f.name.assign(reinterpret_cast<const char*>(blob + e.name_off), e.name_size);
        f.data.assign(data, data + e.data_size);
        pak.files.push_back(eastl::move(f));
    }
    return pak;
}

}  // namespace tryengine::core