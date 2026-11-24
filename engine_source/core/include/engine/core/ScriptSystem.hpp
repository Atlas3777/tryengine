// engine/core/ScriptSystem.hpp
#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <cstddef>
#include <cstdint>
#include <daScript/ast/ast_serializer.h>
#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>
#include <memory>

#include "engine/core/Assert.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::core {

class ScriptSystem;

//! Имя скриптовой функции как non-type template parameter: `InvokeFunctionFast<"EngineUpdate">()`.
//! Строка хранится в самом типе инстанса, поэтому поиск по имени в таблице функций идёт
//! только при первом вызове после смены поколения скриптов.
template <std::size_t N>
struct ScriptFunctionName {
    char value[N]{};

    constexpr ScriptFunctionName(const char (&text)[N]) {
        for (std::size_t i = 0; i < N; ++i)
            value[i] = text[i];
    }

    constexpr const char* c_str() const {
        return value;
    }
};

template <std::size_t N>
ScriptFunctionName(const char (&)[N]) -> ScriptFunctionName<N>;

//! Расширение жизненного цикла скриптов, которое ставит хост. Горячая перезагрузка —
//! именно расширение: редактор подключает её, игра не подключает и о перезагрузке не знает.
class IScriptReloadExtension {
public:
    virtual ~IScriptReloadExtension() = default;

    //! Новый контекст уже установлен, старый ещё жив. Хост повторяет здесь свои точки
    //! входа (EngineStart/EditorStart) до того, как выполнятся [after_reload]-хуки.
    virtual void ReinitializeContext(ScriptSystem& system) = 0;

    //! Попытка перезагрузки завершена. `error` пуст при полном успехе; при `success == true`
    //! непустой `error` означает, что скрипт перезагружен, но хуки сообщили об ошибке.
    virtual void OnReloadFinished(bool success, eastl::string_view error) = 0;
};

// ВАЖНО: порядок полей важен для designated initializers.
struct ScriptSystemConfig {
    eastl::function<void()> register_modules;
    eastl::function<das::smart_ptr<das::FileAccess>()> create_file_access;
    // Необязательный хук: вызывается прямо перед компиляцией главного скрипта.
    eastl::function<void(das::FileAccess&)> prepare_file_access;
    eastl::function<das::Context*(uint32_t stack_size)> create_context;
    das::CodeOfPolicies policies;
};

class ScriptSystem {
public:
    explicit ScriptSystem(ScriptSystemConfig config);
    ~ScriptSystem();

    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    Result<void> LoadMainScript(eastl::string_view path);

    //! Подключает хост-расширение. Без него ReloadMainScript отказывает: перезагрузка
    //! принадлежит хосту, а не движку.
    void SetReloadExtension(IScriptReloadExtension* extension) {
        reload_extension_ = extension;
    }

    //! Пересобирает и симулирует главный скрипт заново, не убивая процесс. Старый контекст
    //! остаётся рабочим до успешной симуляции нового: при ошибке компиляции/симуляции
    //! редактор продолжает жить на старом коде. Перед подменой вызываются функции,
    //! помеченные [before_reload], после подмены и точек входа хоста — [after_reload].
    Result<void> ReloadMainScript();

    //! Вызов из кадрового цикла: имя известно на этапе компиляции, поиск по таблице функций
    //! кешируется до следующей перезагрузки. Исключения скрипта не ловятся — это горячий путь.
    template <ScriptFunctionName Name, typename... Args>
    bool InvokeFunctionFast(Args&&... args) {
        TRY_ASSERT(das_ctx_, "Script context is null");

        auto* function = FindFunction<Name>();
        if (!function)
            return false;

        das::Func custom_func(function);
        das::das_invoke_function<void>::invoke(das_ctx_, nullptr, custom_func, std::forward<Args>(args)...);
        return true;
    }

    //! Вызов с проверкой исключения: ошибка скрипта попадает в лог, а не роняет хост.
    template <ScriptFunctionName Name, typename... Args>
    bool InvokeFunctionSafe(Args&&... args) {
        if (!das_ctx_) {
            LogError(LogCategory::Script, "Script context is null");
            return false;
        }

        auto* function = FindFunction<Name>();
        if (!function)
            return false;

        return InvokeSimFunctionSafe(function, Name.c_str(), std::forward<Args>(args)...);
    }

    //! Вызов с возвращаемым значением. Тип результата — второй template-параметр:
    //! `SimpleReturnUnsafe<"GetEditorViewportSize", das::float2>()`.
    template <ScriptFunctionName Name, typename T, typename... Args>
    Result<T> SimpleReturnUnsafe(Args&&... args) {
        TRY_ASSERT(das_ctx_, "Script context is null");

        auto* function = FindFunction<Name>();
        if (!function)
            return std::unexpected(Error(ErrorCode::EngineInternalError, eastl::string(Name.c_str())));

        das::Func custom_func(function);
        return das::das_invoke_function<T>::invoke(das_ctx_, nullptr, custom_func, std::forward<Args>(args)...);
    }

    das::Context* GetContext() const {
        return das_ctx_;
    }

private:
    // Программа/группа/контекст одного поколения скриптов. Порядок полей = порядок
    // уничтожения: контекст умирает первым, кеш модулей — последним (он владеет FileInfo,
    // на которые ссылаются LineInfo программы).
    struct CompiledScript {
        std::unique_ptr<das::ModuleFileCache> module_cache;
        std::unique_ptr<das::ModuleGroup> module_group;
        das::ProgramPtr program;
        std::unique_ptr<das::Context> context;
    };

    Result<CompiledScript> CompileMainScript(eastl::string_view path);
    //! Вызывает все функции контекста с префиксом имени (их создают аннотации
    //! before_reload/after_reload). Исключение одной функции гасится, чтобы следующая
    //! выполнялась в чистом контексте; итог пишется в errors.
    bool InvokeReloadHooks(das::Context* ctx, const char* prefix, eastl::string& errors);

    //! Поиск функции по имени с кешем на поколение контекста. Проверка ревизии — это
    //! предсказуемый branch: почти всегда она совпадает и таблица функций не сканируется.
    template <ScriptFunctionName Name>
    das::SimFunction* FindFunction() {
        struct Cache {
            uint64_t revision = 0;
            das::SimFunction* function = nullptr;
        };

        static Cache cache;

        if (cache.revision == revision_)
            return cache.function;

        cache.revision = revision_;
        cache.function = das_ctx_->findFunction(Name.c_str());
        if (!cache.function)
            LogError(LogCategory::Script, "Function not found: {}", Name.c_str());
        return cache.function;
    }

    template <typename... Args>
    bool InvokeSimFunctionSafe(das::SimFunction* function, const char* f_name, Args&&... args) {
        if constexpr (sizeof...(Args) == 0) {
            das_ctx_->evalWithCatch(function, nullptr);
        } else {
            vec4f arguments[] = {das::cast<std::decay_t<Args>>::from(std::forward<Args>(args))...};
            das_ctx_->evalWithCatch(function, arguments);
        }

        if (auto ex = das_ctx_->getException()) {
            LogError(LogCategory::Script, "Скрипт упал в функции '{}': {}", f_name, ex);
            return false;
        }

        return true;
    }

    ScriptSystemConfig config_;
    das::smart_ptr<das::FileAccess> file_access_;
    //! Кеш AST для хоста с горячей перезагрузкой: живёт дольше program_/das_ctx_ и обязан
    //! уничтожаться после них.
    std::unique_ptr<das::ModuleFileCache> module_cache_;
    eastl::string main_script_path_;
    std::unique_ptr<das::ModuleGroup> module_group_;
    das::ProgramPtr program_;
    das::Context* das_ctx_ = nullptr;
    //! Уникальный номер поколения контекста. По нему template-кеш выше понимает, что
    //! найденный SimFunction* ещё жив; номера не повторяются между экземплярами системы.
    uint64_t revision_ = 0;
    IScriptReloadExtension* reload_extension_ = nullptr;
};

}  // namespace tryengine::core
