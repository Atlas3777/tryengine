#include <daScript/daScript.h>

#include "engine/core/Log.hpp"
#include "engine/core/LogFileSink.hpp"
#include "engine/core/LogHistory.hpp"

using tryengine::core::LogCategory;
using tryengine::core::LogLevel;

// Биндинг enum'ов журнала. Имя типа в daslang — LogSeverity, а не LogLevel: в namespace das
// уже есть C++ LogLevel (misc/string_writer.h), и typeFactory внутри das разрешил бы
// короткое имя в него вместо нашего enum'а. Порядок значений повторяет engine/core/Log.hpp;
// менять его нельзя без смены обеих сторон.
DAS_BASE_BIND_ENUM(tryengine::core::LogLevel, LogSeverity, Trace, Info, Warning, Error, Critical)
DAS_BASE_BIND_ENUM(tryengine::core::LogCategory, LogCategory, General, Core, Graphics, Physics, Audio, Network,
                   Gameplay, Assert, Importer, Script, FileSystem)

namespace {

char* AllocateString(das::Context* ctx, das::LineInfoArg* at, const eastl::string_view text) {
    if (text.empty())
        return ctx->allocateString("", 0, at);
    return ctx->allocateString(text.data(), static_cast<uint32_t>(text.size()), at);
}

//! Общий путь всех скриптовых LogXxx. at — место вызова в скрипте, поэтому в файле и
//! консоли скриптовая строка адресуется так же, как C++-строка. Категория по умолчанию —
//! Script: она и есть причина, по которой строка появилась.
void ScriptLog(const LogLevel level, const LogCategory category, const char* message, das::LineInfoArg* at) {
    eastl::string_view file;
    uint32_t line = 0;
    if (at != nullptr && at->fileInfo != nullptr) {
        const auto& name = at->fileInfo->name;
        file = eastl::string_view(name.c_str(), name.size());
        line = at->line;
    }
    tryengine::core::Logger::instance().log(level, category, file, line, message != nullptr ? message : "");
}

// Одна пара на уровень: без категории (Script) и с явной. Отдельные C++-имена нужны
// DAS_BIND_FUN — он берёт адрес функции, а у перегрузки адрес неоднозначен. В daslang обе
// функции носят одно имя LogXxx и различаются сигнатурой.
#define TRY_SCRIPT_LOG_LEVEL(FuncName, LevelEnumerator)                                                       \
    void FuncName(const char* message, das::Context*, das::LineInfoArg* at) {                                 \
        ScriptLog(LogLevel::LevelEnumerator, LogCategory::Script, message, at);                               \
    }                                                                                                         \
    void FuncName##Category(LogCategory category, const char* message, das::Context*, das::LineInfoArg* at) { \
        ScriptLog(LogLevel::LevelEnumerator, category, message, at);                                          \
    }

TRY_SCRIPT_LOG_LEVEL(LogTraceScript, Trace)
TRY_SCRIPT_LOG_LEVEL(LogInfoScript, Info)
TRY_SCRIPT_LOG_LEVEL(LogWarnScript, Warning)
TRY_SCRIPT_LOG_LEVEL(LogErrorScript, Error)
TRY_SCRIPT_LOG_LEVEL(LogCriticalScript, Critical)

#undef TRY_SCRIPT_LOG_LEVEL

}  // namespace

//! Очищает кольцо в памяти. Файл на диске не трогается: это история запуска, а не буфер окна.
void LogClear() {
    tryengine::core::LogHistory::instance().clear();
}

uint32_t LogRingCount() {
    return tryengine::core::LogHistory::instance().count();
}

//! Фильтр окна. Маски: бит N — уровень/категория со значением N.
void LogViewSetFilter(uint32_t level_mask, uint32_t category_mask) {
    tryengine::core::LogHistory::instance().set_filter(level_mask, category_mask);
}

uint32_t LogViewCount() {
    return tryengine::core::LogHistory::instance().view_count();
}

uint64_t LogViewNewestSequence() {
    return tryengine::core::LogHistory::instance().view_newest_sequence();
}

LogLevel LogViewLevel(uint32_t index) {
    return tryengine::core::LogHistory::instance().view_level(index);
}

LogCategory LogViewCategory(uint32_t index) {
    return tryengine::core::LogHistory::instance().view_category(index);
}

uint32_t LogViewLine(uint32_t index) {
    return tryengine::core::LogHistory::instance().view_line(index);
}

char* LogViewTime(uint32_t index, das::Context* ctx, das::LineInfoArg* at) {
    eastl::string value;
    if (!tryengine::core::LogHistory::instance().view_time(index, value))
        return AllocateString(ctx, at, {});
    return ctx->allocateString(value.c_str(), static_cast<uint32_t>(value.size()), at);
}

char* LogViewMessage(uint32_t index, das::Context* ctx, das::LineInfoArg* at) {
    eastl::string value;
    if (!tryengine::core::LogHistory::instance().view_message(index, value))
        return AllocateString(ctx, at, {});
    return ctx->allocateString(value.c_str(), static_cast<uint32_t>(value.size()), at);
}

char* LogViewFile(uint32_t index, das::Context* ctx, das::LineInfoArg* at) {
    eastl::string value;
    if (!tryengine::core::LogHistory::instance().view_file(index, value))
        return AllocateString(ctx, at, {});
    return ctx->allocateString(value.c_str(), static_cast<uint32_t>(value.size()), at);
}

int LogLevelCount() {
    return static_cast<int>(LogLevel::Critical) + 1;
}

int LogCategoryCount() {
    return static_cast<int>(LogCategory::FileSystem) + 1;
}

char* LogLevelName(LogLevel level, das::Context* ctx, das::LineInfoArg* at) {
    return AllocateString(ctx, at, tryengine::core::to_string(level));
}

char* LogCategoryName(LogCategory category, das::Context* ctx, das::LineInfoArg* at) {
    return AllocateString(ctx, at, tryengine::core::to_string(category));
}

//! Путь файла журнала текущего запуска; пусто, если файловый sink не установлен.
char* LogFilePath(das::Context* ctx, das::LineInfoArg* at) {
    return AllocateString(ctx, at, tryengine::core::ActiveLogFilePath());
}

class Module_Log : public das::Module {
public:
    Module_Log() : das::Module("tryLog") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();

        addEnumeration(new EnumerationLogSeverity());
        addEnumeration(new EnumerationLogCategory());

        das::addExtern<DAS_BIND_FUN(LogTraceScript)>(*this, lib, "LogTrace", das::SideEffects::accessGlobal,
                                                     "LogTraceScript");
        das::addExtern<DAS_BIND_FUN(LogTraceScriptCategory)>(*this, lib, "LogTrace", das::SideEffects::accessGlobal,
                                                             "LogTraceScriptCategory");
        das::addExtern<DAS_BIND_FUN(LogInfoScript)>(*this, lib, "LogInfo", das::SideEffects::accessGlobal,
                                                    "LogInfoScript");
        das::addExtern<DAS_BIND_FUN(LogInfoScriptCategory)>(*this, lib, "LogInfo", das::SideEffects::accessGlobal,
                                                            "LogInfoScriptCategory");
        das::addExtern<DAS_BIND_FUN(LogWarnScript)>(*this, lib, "LogWarn", das::SideEffects::accessGlobal,
                                                    "LogWarnScript");
        das::addExtern<DAS_BIND_FUN(LogWarnScriptCategory)>(*this, lib, "LogWarn", das::SideEffects::accessGlobal,
                                                            "LogWarnScriptCategory");
        das::addExtern<DAS_BIND_FUN(LogErrorScript)>(*this, lib, "LogError", das::SideEffects::accessGlobal,
                                                     "LogErrorScript");
        das::addExtern<DAS_BIND_FUN(LogErrorScriptCategory)>(*this, lib, "LogError", das::SideEffects::accessGlobal,
                                                             "LogErrorScriptCategory");
        das::addExtern<DAS_BIND_FUN(LogCriticalScript)>(*this, lib, "LogCritical", das::SideEffects::accessGlobal,
                                                        "LogCriticalScript");
        das::addExtern<DAS_BIND_FUN(LogCriticalScriptCategory)>(
            *this, lib, "LogCritical", das::SideEffects::accessGlobal, "LogCriticalScriptCategory");
        das::addExtern<DAS_BIND_FUN(LogClear)>(*this, lib, "LogClear", das::SideEffects::accessGlobal, "LogClear");
        das::addExtern<DAS_BIND_FUN(LogRingCount)>(*this, lib, "LogRingCount", das::SideEffects::accessGlobal,
                                                   "LogRingCount");
        das::addExtern<DAS_BIND_FUN(LogViewSetFilter)>(*this, lib, "LogViewSetFilter", das::SideEffects::accessGlobal,
                                                       "LogViewSetFilter");
        das::addExtern<DAS_BIND_FUN(LogViewCount)>(*this, lib, "LogViewCount", das::SideEffects::accessGlobal,
                                                   "LogViewCount");
        das::addExtern<DAS_BIND_FUN(LogViewNewestSequence)>(*this, lib, "LogViewNewestSequence",
                                                            das::SideEffects::accessGlobal, "LogViewNewestSequence");
        das::addExtern<DAS_BIND_FUN(LogViewLevel)>(*this, lib, "LogViewLevel", das::SideEffects::accessGlobal,
                                                   "LogViewLevel");
        das::addExtern<DAS_BIND_FUN(LogViewCategory)>(*this, lib, "LogViewCategory", das::SideEffects::accessGlobal,
                                                      "LogViewCategory");
        das::addExtern<DAS_BIND_FUN(LogViewLine)>(*this, lib, "LogViewLine", das::SideEffects::accessGlobal,
                                                  "LogViewLine");
        das::addExtern<DAS_BIND_FUN(LogViewTime)>(*this, lib, "LogViewTime", das::SideEffects::accessGlobal,
                                                  "LogViewTime");
        das::addExtern<DAS_BIND_FUN(LogViewMessage)>(*this, lib, "LogViewMessage", das::SideEffects::accessGlobal,
                                                     "LogViewMessage");
        das::addExtern<DAS_BIND_FUN(LogViewFile)>(*this, lib, "LogViewFile", das::SideEffects::accessGlobal,
                                                  "LogViewFile");
        das::addExtern<DAS_BIND_FUN(LogLevelCount)>(*this, lib, "LogLevelCount", das::SideEffects::none,
                                                    "LogLevelCount");
        das::addExtern<DAS_BIND_FUN(LogCategoryCount)>(*this, lib, "LogCategoryCount", das::SideEffects::none,
                                                       "LogCategoryCount");
        das::addExtern<DAS_BIND_FUN(LogLevelName)>(*this, lib, "LogLevelName", das::SideEffects::none, "LogLevelName");
        das::addExtern<DAS_BIND_FUN(LogCategoryName)>(*this, lib, "LogCategoryName", das::SideEffects::none,
                                                      "LogCategoryName");
        das::addExtern<DAS_BIND_FUN(LogFilePath)>(*this, lib, "LogFilePath", das::SideEffects::none, "LogFilePath");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Log, Module_Log);
REGISTER_MODULE(Module_Log);
