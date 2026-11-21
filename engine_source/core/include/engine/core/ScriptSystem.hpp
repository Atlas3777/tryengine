// engine/core/ScriptSystem.hpp
#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>
#include <memory>

#include "engine/core/Assert.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::core {

class ScriptSystem;

// ВАЖНО: порядок полей важен для designated initializers.
struct ScriptSystemConfig {
    eastl::function<void()> register_modules;
    eastl::function<das::smart_ptr<das::FileAccess>()> create_file_access;
    // Необязательный хук: вызывается прямо перед компиляцией главного скрипта.
    eastl::function<void(das::FileAccess&)> prepare_file_access;
    eastl::function<das::Context*(uint32_t stack_size)> create_context;
    das::CodeOfPolicies policies;
    // Необязательный хук горячей перезагрузки: новый контекст уже установлен, нужно
    // повторить в нём точки входа хоста (EngineStart/EditorStart), но ещё не вызваны
    // [after_reload]-хуки скрипта.
    eastl::function<void(ScriptSystem&)> reinit_context;
};

class ScriptSystem {
public:
    explicit ScriptSystem(ScriptSystemConfig config);
    ~ScriptSystem();

    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    Result<void> LoadMainScript(eastl::string_view path);

    //! Пересобирает и симулирует главный скрипт заново, не убивая процесс. Старый контекст
    //! остаётся рабочим до успешной симуляции нового: при ошибке компиляции/симуляции
    //! редактор продолжает жить на старом коде. Перед подменой вызываются функции,
    //! помеченные [before_reload], после подмены и точек входа хоста — [after_reload].
    Result<void> ReloadMainScript();

    //! Запрос на перезагрузку из UI/хоткея. Сама перезагрузка делается в кадровом цикле,
    //! когда ни одна функция скрипта ещё не выполняется.
    void RequestReload() {
        reload_requested_ = true;
    }

    bool IsReloadRequested() const {
        return reload_requested_;
    }

    void ClearReloadRequest() {
        reload_requested_ = false;
    }

    bool IsReloading() const {
        return reloading_;
    }

    //! Пусто — перезагрузок ещё не было; иначе итог последней попытки.
    const eastl::string& ReloadStatus() const {
        return reload_status_;
    }

    template <typename... Args>
    bool InvokeFunctionSafe(eastl::string_view f_name, Args&&... args) {
        if (!das_ctx_) {
            LogError(LogCategory::Script, "Script context is null");
            return false;
        }

        auto* function = das_ctx_->findFunction(f_name.data());
        if (!function) {
            LogInfo(LogCategory::Script, "Function not found: {}", f_name.data());
            return false;
        }

        return InvokeSimFunctionSafe(function, f_name, std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    Result<T> SimpleReturnUnsafe(eastl::string_view f_name, Args&&... args) {
        TRY_ASSERT(das_ctx_, "Script context is null");
        auto* function = das_ctx_->findFunction(f_name.data());

        if (!function)
            return LogAndMakeError("Function not found: {}", f_name);

        das::Func custom_func(function);
        return das::das_invoke_function<T>::invoke(das_ctx_, nullptr, custom_func, std::forward<Args>(args)...);
    }

    template <typename... Args>
    bool InvokeFunctionFast(eastl::string_view f_name, Args&&... args) {
        TRY_ASSERT(das_ctx_, "Script context is null");

        auto* function = das_ctx_->findFunction(f_name.data());
        if (!function) {
            LogError(LogCategory::Script, "Function not found: {}", f_name);
            return false;
        }

        das::Func custom_func(function);
        das::das_invoke_function<void>::invoke(das_ctx_, nullptr, custom_func, std::forward<Args>(args)...);
        return true;
    }

    das::Context* GetContext() const {
        return das_ctx_;
    }

private:
    // Программа/группа/контекст одного поколения скриптов. Порядок полей = порядок
    // уничтожения: контекст умирает первым, группа модулей — последней.
    struct CompiledScript {
        std::unique_ptr<das::ModuleGroup> module_group;
        das::ProgramPtr program;
        std::unique_ptr<das::Context> context;
    };

    Result<CompiledScript> CompileMainScript(eastl::string_view path);
    //! Вызывает все функции контекста с префиксом имени (их создают аннотации
    //! before_reload/after_reload). Исключение одной функции гасится, чтобы следующая
    //! выполнялась в чистом контексте; итог пишется в errors.
    bool InvokeReloadHooks(das::Context* ctx, const char* prefix, eastl::string& errors);

    template <typename... Args>
    bool InvokeSimFunctionSafe(das::SimFunction* function, eastl::string_view f_name, Args&&... args) {
        if constexpr (sizeof...(Args) == 0) {
            das_ctx_->evalWithCatch(function, nullptr);
        } else {
            vec4f arguments[] = {das::cast<std::decay_t<Args>>::from(std::forward<Args>(args))...};
            das_ctx_->evalWithCatch(function, arguments);
        }

        if (auto ex = das_ctx_->getException()) {
            LogError(LogCategory::Script, "Скрипт упал в функции '{}': {}", f_name.data(), ex);
            return false;
        }

        return true;
    }

    ScriptSystemConfig config_;
    das::smart_ptr<das::FileAccess> file_access_;
    eastl::string main_script_path_;
    std::unique_ptr<das::ModuleGroup> module_group_;
    das::ProgramPtr program_;
    das::Context* das_ctx_ = nullptr;
    eastl::string reload_status_;
    bool reload_requested_ = false;
    bool reloading_ = false;
};

}  // namespace tryengine::core
