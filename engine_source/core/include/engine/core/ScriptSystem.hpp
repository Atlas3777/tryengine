#pragma once

#include <daScript/daScript.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

#include "Engine.hpp"
#include "Result.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/MakeError.hpp"

namespace tryengine::core {

// Режимы реакции на ошибку компиляции при Live Coding
enum class ReloadErrorPolicy {
    ContinueWithOldContext,  // Продолжить игру на последней рабочей версии кода (старый контекст тикает)
    FreezeExecution          // Поставить обновление на паузу (update не вызывается) до исправления ошибок
};

// TODO: С копированием разобраться
class TryengineContext : public das::Context {
public:
    Engine& engine;

    // using Context::Context;

    TryengineContext(Engine& eng, uint32_t stackSize = 16 * 1024, bool ph = false)
        : Context(stackSize, ph), engine(eng) {}
};

using ContextFactory = std::function<das::Context*(Engine& engine, uint32_t stack_size)>;

class ScriptSystem {
public:
    explicit ScriptSystem(Engine& engine, const std::string& path = "");
    ~ScriptSystem();
    void SetContextFactory(ContextFactory factory) { context_factory_ = std::move(factory); }

    Result<void> LoadMainScript(const std::string& path);
    void InvokeStart();
    void InvokeUpdate(float time);

    void CheckForReload(float dt);

    template <typename... Args>
    bool InvokeFunctionSafe(const std::string& f_name, Args&&... args) {
        auto function = das_ctx ? das_ctx->findFunction(f_name.c_str()) : nullptr;

        if (!das_ctx || !function) {
            LogInfo(LogCategory::Script, "Function not found: {}", f_name);
            return false;
        }

        return InvokeSimFunctionSafe(function, f_name, std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    Result<T> SimpleReturnUnsafe(const eastl::string_view f_name, Args&&... args) {
        TRY_ASSERT(das_ctx, "Нет контекста");
        auto function = das_ctx->findFunction(f_name.data());

        if (!function)
            return LogAndMakeError("Function not found: {}", f_name);

        das::Func custom_func(function);
        return das::das_invoke_function<T>::invoke(das_ctx, nullptr, custom_func, std::forward<Args>(args)...);
    }

    template <typename... Args>
    bool InvokeFunctionFast(const std::string& f_name, Args&&... args) {
        auto function = das_ctx ? das_ctx->findFunction(f_name.c_str()) : nullptr;

        if (!das_ctx || !function) {
            LogInfo(LogCategory::Script, "Function not found: {}", f_name);
            return false;
        }

        das::Func custom_func(function);
        das::das_invoke_function<void>::invoke(das_ctx, nullptr, custom_func, std::forward<Args>(args)...);
        return true;
    }

    das::Context* GetContext();
    void SetReloadErrorPolicy(ReloadErrorPolicy policy) { error_policy_ = policy; }
    bool IsFrozen() const { return is_frozen_; }

private:
    Result<void> CompileAndLoad(const std::string& path);
    void InvokeHook(const std::string& hook_substring);

    template <typename... Args>
    bool InvokeSimFunctionSafe(das::SimFunction* function, const std::string& f_name, Args&&... args) {
        if constexpr (sizeof...(Args) == 0) {
            das_ctx->evalWithCatch(function, nullptr);
        } else {
            vec4f arguments[] = {das::cast<std::decay_t<Args>>::from(std::forward<Args>(args))...};
            das_ctx->evalWithCatch(function, arguments);
        }

        if (auto ex = das_ctx->getException()) {
            LogInfo(LogCategory::Script, "Скрипт упал в функции '{}': {}", f_name, ex);
            return false;
        }

        return true;
    }

    Engine& engine_;
    das::Context* CreateContext(uint32_t stack_size);
    ContextFactory context_factory_ = nullptr;

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