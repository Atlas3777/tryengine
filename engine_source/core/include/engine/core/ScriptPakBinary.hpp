#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "engine/core/Result.hpp"

// Бинарный формат пака скриптов. Little-endian, без выравнивания данных.
//
//   [ScriptPakHeader][ScriptPakFileEntry x file_count][blob]
//
// blob: имена и исходники подряд; off в записях отсчитывается от blob_off.
//
// ВАЖНО: любое изменение структур ниже ломает static_assert'ы.
// Поменял структуру -> обнови assert'ы И подними kScriptPakVersion.

namespace tryengine::core {

static_assert(std::endian::native == std::endian::little, "ScriptPak: формат рассчитан на little-endian");

inline constexpr uint32_t kScriptPakMagic = 0x4B505354;  // "TSPK"
inline constexpr uint32_t kScriptPakVersion = 1;

struct ScriptPakHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;   // размер всего файла
    uint32_t file_count;
    uint32_t table_off;    // от начала файла
    uint32_t blob_off;     // от начала файла
    uint32_t entry_index;  // индекс главного скрипта в таблице
    uint32_t reserved;     // 0
};

struct ScriptPakFileEntry {
    uint32_t name_off;
    uint32_t name_size;
    uint32_t data_off;
    uint32_t data_size;
    uint32_t data_hash;  // FNV-1a 32 по данным
};

// ---- защита от случайных изменений формата ----
static_assert(std::is_standard_layout_v<ScriptPakHeader> && std::is_trivially_copyable_v<ScriptPakHeader>);
static_assert(sizeof(ScriptPakHeader) == 32, "ScriptPakHeader изменился: подними kScriptPakVersion");
static_assert(offsetof(ScriptPakHeader, magic) == 0);
static_assert(offsetof(ScriptPakHeader, version) == 4);
static_assert(offsetof(ScriptPakHeader, total_size) == 8);
static_assert(offsetof(ScriptPakHeader, file_count) == 12);
static_assert(offsetof(ScriptPakHeader, table_off) == 16);
static_assert(offsetof(ScriptPakHeader, blob_off) == 20);
static_assert(offsetof(ScriptPakHeader, entry_index) == 24);
static_assert(offsetof(ScriptPakHeader, reserved) == 28);

static_assert(std::is_standard_layout_v<ScriptPakFileEntry> && std::is_trivially_copyable_v<ScriptPakFileEntry>);
static_assert(sizeof(ScriptPakFileEntry) == 20, "ScriptPakFileEntry изменился: подними kScriptPakVersion");
static_assert(offsetof(ScriptPakFileEntry, name_off) == 0);
static_assert(offsetof(ScriptPakFileEntry, name_size) == 4);
static_assert(offsetof(ScriptPakFileEntry, data_off) == 8);
static_assert(offsetof(ScriptPakFileEntry, data_size) == 12);
static_assert(offsetof(ScriptPakFileEntry, data_hash) == 16);

// ---- in-memory представление ----
struct ScriptPakFile {
    eastl::string name;  // путь как его видит компилятор (см. ToPakName)
    eastl::vector<uint8_t> data;
};

struct ScriptPak {
    eastl::vector<ScriptPakFile> files;
    uint32_t entry_index = 0;

    const eastl::string& Entry() const { return files[entry_index].name; }
};

uint32_t Fnv1a32(eastl::span<const uint8_t> bytes);

eastl::vector<uint8_t> SerializeScriptPak(const ScriptPak& pak);
Result<ScriptPak> DeserializeScriptPak(eastl::span<const uint8_t> bytes);

}  // namespace tryengine::core