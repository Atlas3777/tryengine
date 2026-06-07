#include "engine/core/ScriptSystem.hpp"

#include <daScript/ast/dyn_modules.h>
#include <daScript/simulate/aot.h>
#include <daScript/simulate/runtime_array.h>
#include <iostream>
#include <set>

inline void InitializeDaScriptModules() {
    NEED_ALL_DEFAULT_MODULES;
}

namespace tryengine::core {

// Кастомный FileAccess для автоматического перехвата всех зависимостей (require) скрипта
class TrackingFileAccess : public das::FsFileAccess {
public:
    TrackingFileAccess(const das::string& pak, const das::smart_ptr<das::FsFileAccess>& pac)
        : das::FsFileAccess(pak, pac) {}

    das::FileInfo* getNewFileInfo(const das::string& fileName) override {
        das::FileInfo* info = das::FsFileAccess::getNewFileInfo(fileName);
        if (info) {
            tracked_files.insert(fileName.c_str());
        }
        return info;
    }

    std::set<std::string> tracked_files;
};

ScriptSystem::ScriptSystem() {
    das::setDasRoot(DAS_ROOT_DIR);
    InitializeDaScriptModules();

    das::TextPrinter tout;
    das::vector<das::string> load_modules;
    auto fAccess = das::make_smart<das::FsFileAccess>();
    das::require_dynamic_modules(fAccess, das::getDasRoot(), "./", load_modules, tout);

    das::Module::Initialize();
}

ScriptSystem::~ScriptSystem() {
    fn_start = nullptr;
    fn_update = nullptr;

    if (das_ctx) {
        delete das_ctx;
        das_ctx = nullptr;
    }
    das::Module::Shutdown();
}

bool ScriptSystem::LoadMainScript(const std::string& path) {
    main_script_path_ = path;
    return CompileAndLoad(path);
}

bool ScriptSystem::CompileAndLoad(const std::string& path) {
    das::TextPrinter tout;
    das::ModuleGroup dummyLibGroup;

    auto fAccess = das::make_smart<TrackingFileAccess>("project.das_project", das::make_smart<das::FsFileAccess>());
    auto program = das::compileDaScript(path, fAccess, tout, dummyLibGroup);

    if (program->failed()) {
        std::cerr << "[LiveCoding] Ошибка компиляции!\n" << tout.str() << "\n";
        for (const auto& error : program->errors) {
            std::cerr << reportError(error.at, error.what, error.extra, error.fixme, error.cerr) << "\n";
        }
        return false;
    }

    auto new_ctx = new das::Context(program->getContextStackSize());
    if (!program->simulate(*new_ctx, tout)) {
        std::cerr << "[LiveCoding] Ошибка симуляции контекста!\n";
        delete new_ctx;
        return false;
    }

    if (das_ctx) {
        delete das_ctx;
    }

    das_ctx = new_ctx;
    fn_start = das_ctx->findFunction("start");
    fn_update = das_ctx->findFunction("update");

    // Обновляем список отслеживаемых файлов новыми данными из fAccess
    file_watch_map_.clear();
    for (const auto& file_path : fAccess->tracked_files) {
        std::error_code ec;
        auto last_write = std::filesystem::last_write_time(file_path, ec);
        if (!ec) {
            file_watch_map_[file_path] = last_write;
        }
    }

    std::cout << "[LiveCoding] Скрипт успешно скомпилирован.\n";
    return true;
}

void ScriptSystem::CheckForReload(float dt) {
    reload_timer_ += dt;
    if (reload_timer_ < 0.5f) return;
    reload_timer_ = 0.0f;

    bool need_reload = false;

    // Если первая компиляция упала и карта пуста — принудительно ставим на слежку главный файл
    if (file_watch_map_.empty() && std::filesystem::exists(main_script_path_)) {
        std::error_code ec;
        auto current_time = std::filesystem::last_write_time(main_script_path_, ec);
        if (!ec) {
            file_watch_map_[main_script_path_] = current_time;
            need_reload = true;
        }
    }

    // Проверяем изменения во всех зависимостях скрипта
    for (auto& [path, last_time] : file_watch_map_) {
        if (std::filesystem::exists(path)) {
            std::error_code ec;
            auto current_time = std::filesystem::last_write_time(path, ec);
            if (!ec && last_time != current_time) {
                last_time = current_time; // Фиксируем изменение сразу, предотвращая бесконечные циклы
                need_reload = true;
            }
        }
    }

    if (!need_reload) return;

    std::cout << "[LiveCoding] Изменение обнаружено. Перезагрузка скриптов...\n";

    // Шаг 1: Сохраняем состояние @live переменных текущего контекста
    InvokeHook("__before_reload_live_vars");

    // Шаг 2: Пробуем скомпилировать
    if (CompileAndLoad(main_script_path_)) {
        // Успех: сбрасываем заморозку, восстанавливаем переменные в новый контекст и вызываем старт
        if (is_frozen_) {
            std::cout << "[LiveCoding] Ошибки исправлены! Размораживаем выполнение скрипта.\n";
            is_frozen_ = false;
        }
        InvokeHook("__after_reload_live_vars");
        InvokeStart();
    } else {
        // Ошибка: обрабатываем в зависимости от выбранной политики
        if (error_policy_ == ReloadErrorPolicy::FreezeExecution) {
            is_frozen_ = true;
            std::cerr << "[LiveCoding] Выполнение заблокировано (ПАУЗА) до исправления ошибок.\n";
        } else {
            is_frozen_ = false;
            std::cerr << "[LiveCoding] Продолжаем работу на старом контексте. Ждем исправления...\n";
        }
    }
}

void ScriptSystem::InvokeHook(const std::string& hook_substring) {
    if (!das_ctx) return;

    for (int i = 0; i < das_ctx->getTotalFunctions(); ++i) {
        auto* fn = das_ctx->getFunction(i);
        if (fn && fn->name) {
            std::string name = fn->name;
            // Ищем подстроку, чтобы успешно находить хуки внутри пространств имен (например, `live_vars::__before_reload_live_vars`)
            if (name.find(hook_substring) != std::string::npos) {
                std::cout << "[LiveCoding] Вызов хука: " << name << "\n";
                das::Func f(fn);
                das::das_invoke_function<void>::invoke(das_ctx, nullptr, f);
            }
        }
    }
}

void ScriptSystem::InvokeStart() {
    if (das_ctx && fn_start) {
        das::Func start_func(fn_start);
        das::das_invoke_function<void>::invoke(das_ctx, nullptr, start_func);
    }
}

void ScriptSystem::InvokeUpdate(float dt) {
    // КРИТИЧЕСКИЙ МОМЕНТ: если система заморожена из-за ошибки компиляции, игнорируем тик обновления
    if (is_frozen_) return;

    if (das_ctx && fn_update) {
        das::Func update_func(fn_update);
        das::das_invoke_function<void>::invoke(das_ctx, nullptr, update_func, dt);
    }
}

das::Context* ScriptSystem::GetContext() {
    return das_ctx;
}

}  // namespace tryengine::core