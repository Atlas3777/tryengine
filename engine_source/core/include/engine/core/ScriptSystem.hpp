#pragma once

#include <daScript/daScript.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

namespace tryengine::core {

// Режимы реакции на ошибку компиляции при Live Coding
enum class ReloadErrorPolicy {
    ContinueWithOldContext,  // Продолжить игру на последней рабочей версии кода (старый контекст тикает)
    FreezeExecution          // Поставить обновление на паузу (update не вызывается) до исправления ошибок
};

class ScriptSystem {
public:
    ScriptSystem();
    ~ScriptSystem();

    // Загрузка и жизненный цикл главного скрипта
    bool LoadMainScript(const std::string& path);
    void InvokeStart();
    void InvokeUpdate(float dt);

    // Проверка изменений файлов на диске (Live Coding)
    void CheckForReload(float dt);

    template<typename... Args>
    bool InvokeFunction(const std::string& f_name, Args&&... args) {
        auto function = das_ctx->findFunction(f_name.c_str());

        if (das_ctx && function) {
            das::Func custom_func(function);
            das::das_invoke_function<void>::invoke(das_ctx, nullptr, custom_func, std::forward<Args>(args)...);
        }
        else {
            std::cout << "[ScriptSystem] Function not found: " << f_name << std::endl;
        }
        return function;
    }

    // Геттеры и настройки
    das::Context* GetContext();
    void SetReloadErrorPolicy(ReloadErrorPolicy policy) { error_policy_ = policy; }
    bool IsFrozen() const { return is_frozen_; }

private:
    bool CompileAndLoad(const std::string& path);
    void InvokeHook(const std::string& hook_substring);

    std::string main_script_path_;

    // Хранит пути ко всем зависимостям (включая require) и время их изменения
    std::unordered_map<std::string, std::filesystem::file_time_type> file_watch_map_;
    std::unordered_map<std::string, das::SimFunction*> finded_function;

    float reload_timer_ = 0.0f;
    ReloadErrorPolicy error_policy_ = ReloadErrorPolicy::FreezeExecution;  // По умолчанию замораживаем
    bool is_frozen_ = false;                                               // Флаг состояния паузы

    // Контекст и функции daScript
    das::Context* das_ctx = nullptr;
    das::SimFunction* fn_start = nullptr;
    das::SimFunction* fn_update = nullptr;
};

}  // namespace tryengine::core
