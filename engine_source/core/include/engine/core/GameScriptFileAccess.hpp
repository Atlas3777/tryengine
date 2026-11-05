#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <daScript/daScript.h>

#include "engine/core/Result.hpp"
#include "engine/core/ScriptPakBinary.hpp"

namespace tryengine::core {

// Пути внутри das root хранятся как "%/daslib/x.das", чтобы пак не зависел от места установки daslang.
eastl::string ToPakName(const das::string& path);
das::string FromPakName(const eastl::string& pak_name);

// Собирает пак из уже скомпилированной программы:
// главный скрипт + файлы всех модулей из thisModuleGroup с непустым fileName.
// Нативные модули (fileName пустой) пропускаются - они линкуются в exe.
Result<ScriptPak> CollectScriptPak(das::FileAccess& access, das::ModuleGroup& group,
                                   const das::string& entry,
                                   const eastl::vector<das::string>& extra);

// Отдаёт исходники из памяти, диск не трогает.
// getModuleInfo: ищет файл по имени модуля (stem файла == последний сегмент require).
class PackedFileAccess : public das::FileAccess {
public:
    explicit PackedFileAccess(ScriptPak pak);

    das::ModuleInfo getModuleInfo(const das::string& req, const das::string& from) const override;
    const eastl::string& EntryPath() const { return entry_path_; }

protected:
    das::FileInfo* getNewFileInfo(const das::string& file_name) override;

private:
    ScriptPak pak_;
    eastl::string entry_path_;                                           // развёрнутый путь
    eastl::vector<eastl::string> paths_;                                 // развёрнутые пути, индекс = индекс файла
    eastl::hash_map<eastl::string, size_t> by_path_;
    eastl::hash_map<eastl::string, eastl::vector<size_t>> by_stem_;
};

}  // namespace tryengine::core