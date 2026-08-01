#pragma once

#include <daScript/daScript.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

#include "engine/core/Assert.hpp"  // TRY_CHECK / TRY_LOG_INFO

namespace tryengine::core {

// Режимы реакции на ошибку компиляции при Live Coding
enum class ReloadErrorPolicy {
    ContinueWithOldContext,  // Продолжить игру на последней рабочей версии кода (старый контекст тикает)
    FreezeExecution          // Поставить обновление на паузу (update не вызывается) до исправления ошибок
};

class ScriptSystem {
public:
    ScriptSystem(const std::string& path);
    ~ScriptSystem();

    // Загрузка и жизненный цикл главного скрипта
    bool LoadMainScript(const std::string& path);
    void InvokeStart();
    void InvokeUpdate(float dt);

    // Проверка изменений файлов на диске (Live Coding)
    void CheckForReload(float dt);

    // Безопасный вызов: реализован через ctx->evalWithCatch (см.
    // InvokeSimFunctionSafe ниже). Любой panic() внутри скрипта будет
    // пойман и залогирован через TRY_LOG_INFO с именем функции и текстом
    // ошибки, приложение не упадет. Чуть медленнее из-за самого
    // evalWithCatch и проверки ctx->getException() — используйте это по
    // умолчанию и везде, где не уверены на 100%, что скрипт отработает без
    // ошибок.
    //
    // Ограничение: аргументы марашаллятся вручную через cast<T>::from, а он
    // поддерживает только примитивы daScript (int32_t/uint32_t/int64_t/
    // float/double/bool/char*/указатели). Если понадобится передавать
    // строки как std::string или сложные структуры — их нужно будет явно
    // привести к поддерживаемому типу перед вызовом.
    template<typename... Args>
    bool InvokeFunctionSafe(const std::string& f_name, Args&&... args) {
        auto function = das_ctx ? das_ctx->findFunction(f_name.c_str()) : nullptr;

        if (!das_ctx || !function) {
            TRY_LOG_INFO("[ScriptSystem] Function not found: {}", f_name);
            return false;
        }

        return InvokeSimFunctionSafe(function, f_name, std::forward<Args>(args)...);
    }

    // Быстрый вызов без перехвата ошибок: используйте только тогда, когда вы
    // на 100% уверены, что данная скриптовая функция не может упасть
    // (например вызывается в очень горячем цикле и уже проверена
    // InvokeFunctionSafe / verifyCall на этапе разработки). Если скрипт всё
    // же запаникует — приложение упадет так же, как и раньше.
    template<typename... Args>
    bool InvokeFunctionFast(const std::string& f_name, Args&&... args) {
        auto function = das_ctx ? das_ctx->findFunction(f_name.c_str()) : nullptr;

        if (!das_ctx || !function) {
            TRY_LOG_INFO("[ScriptSystem] Function not found: {}", f_name);
            return false;
        }

        das::Func custom_func(function);
        das::das_invoke_function<void>::invoke(das_ctx, nullptr, custom_func, std::forward<Args>(args)...);
        return true;
    }

    // Оставлено для обратной совместимости со старым кодом — по умолчанию
    // безопасно (см. InvokeFunctionSafe). Новый код лучше вызывает
    // InvokeFunctionSafe / InvokeFunctionFast явно.
    template<typename... Args>
    bool InvokeFunction(const std::string& f_name, Args&&... args) {
        return InvokeFunctionSafe(f_name, std::forward<Args>(args)...);
    }

    // Геттеры и настройки
    das::Context* GetContext();
    void SetReloadErrorPolicy(ReloadErrorPolicy policy) { error_policy_ = policy; }
    bool IsFrozen() const { return is_frozen_; }

private:
    bool CompileAndLoad(const std::string& path);
    void InvokeHook(const std::string& hook_substring);

    // Общая безопасная точка вызова SimFunction*, когда он у нас уже есть
    // (не нужно искать по имени заново). Используется для fn_start,
    // fn_update и live-coding хуков — именно там раньше падало без единого
    // сообщения о том, какая функция виновата.
    //
    // ВАЖНО: сознательно НЕ используем das_invoke_function здесь — он должен
    // бросать C++ исключение при panic() внутри скрипта, но на практике это
    // исключение не всегда долетает до try/catch (сборка без exceptions,
    // паника внутри AOT-кода и т.п.), и приложение падает молча. Вместо
    // этого напрямую собираем vec4f-аргументы (cast<T>::from) и зовем
    // ctx->evalWithCatch — это низкоуровневый механизм, который ловит панику
    // внутри самого daScript-рантайма и не зависит от C++ exceptions.
    template<typename... Args>
    bool InvokeSimFunctionSafe(das::SimFunction* function, const std::string& f_name, Args&&... args) {
        if constexpr (sizeof...(Args) == 0) {
            das_ctx->evalWithCatch(function, nullptr);
        } else {
            vec4f arguments[] = { das::cast<std::decay_t<Args>>::from(std::forward<Args>(args))... };
            das_ctx->evalWithCatch(function, arguments);
        }

        if (auto ex = das_ctx->getException()) {
            TRY_LOG_INFO("[ScriptSystem] Скрипт упал в функции '{}': {}", f_name, ex);
            return false;
        }

        return true;
    }

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