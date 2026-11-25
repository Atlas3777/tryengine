// engine/core/ScriptSystem.hpp
#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <daScript/ast/ast_serializer.h>
#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>
#include <memory>
#include <mutex>
#include <optional>

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

//! Состояние поколения скриптов. `Compiling` — идёт фоновая компиляция/симуляция; в это
//! время вызовы скриптов запрещены, потому что daScript не потокобезопасен. `Failed` —
//! первая загрузка не удалась, движок заперт и скриптовой части больше нет.
enum class ScriptStatus : uint8_t {
    Unloaded,
    Compiling,
    Ready,
    Failed,
};

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
    //! Исполнитель фоновой компиляции: принимает работу и выполняет её вне главного
    //! потока. Хосты передают сюда ThreadPool(); пусто — компиляция идёт на месте
    //! (CLI, тесты, старые синхронные сценарии).
    eastl::function<void(eastl::function<void()>)> post_background;
};

class ScriptSystem {
public:
    explicit ScriptSystem(ScriptSystemConfig config);
    ~ScriptSystem();

    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    //! Синхронная загрузка: компилирует и симулирует на вызывающем потоке, затем
    //! устанавливает контекст. Хосты используют её в CLI/тестах; кадровый цикл зовёт
    //! LoadMainScriptAsync, чтобы не блокировать окно.
    Result<void> LoadMainScript(eastl::string_view path);

    //! Ставит компиляцию первой загрузки в фон. Возвращается сразу; результат забирает
    //! PollAsync() на главном потоке. Если фоновый исполнитель не задан, компиляция идёт
    //! на месте, но контекст всё равно устанавливается только в PollAsync().
    void LoadMainScriptAsync(eastl::string_view path);

    //! Запускает горячую перезагрузку в фоне. Старый контекст остаётся живым и перестаёт
    //! вызываться до PollAsync(), который либо подменит его, либо оставит старый.
    void ReloadMainScriptAsync();

    //! Повторяет первую загрузку после ScriptStatus::Failed, не перезапуская процесс:
    //! свежий FileAccess перечитывает исходники с диска (редактор — F5/Retry в оверлее).
    //! Возвращает false, если повтор сейчас невозможен (не Failed / уже идёт компиляция).
    bool RetryMainScriptAsync();

    //! Главный поток: забирает готовую фоновую компиляцию. Единственное место, где
    //! устанавливается новое поколение контекста и вызываются reload-хуки.
    void PollAsync();

    //! Подключает хост-расширение. Без него ReloadMainScriptAsync отказывает: перезагрузка
    //! принадлежит хосту, а не движку.
    void SetReloadExtension(IScriptReloadExtension* extension) {
        reload_extension_ = extension;
    }

    //! Синхронная перезагрузка. Оставлена для тестов и хостов без кадрового цикла; редактор
    //! зовёт ReloadMainScriptAsync.
    Result<void> ReloadMainScript();

    //! Контекст установлен и в нём не идёт компиляция: можно вызывать скриптовые функции.
    [[nodiscard]] bool IsReady() const {
        return das_ctx_ != nullptr && !compiling_;
    }

    [[nodiscard]] ScriptStatus GetStatus() const {
        return status_;
    }

    [[nodiscard]] bool IsCompiling() const {
        return status_ == ScriptStatus::Compiling;
    }

    //! Первая загрузка провалилась: контекста нет, движок заперт. Хост не должен вызывать
    //! скрипты и не пытается восстановиться сам — только новый процесс.
    [[nodiscard]] bool IsFailed() const {
        return status_ == ScriptStatus::Failed;
    }

    [[nodiscard]] bool HasContext() const {
        return das_ctx_ != nullptr;
    }

    //! Последняя попытка компиляции закончилась ошибкой. При успешной перезагрузке
    //! сбрасывается; редактор по этому флагу запрещает Play.
    [[nodiscard]] bool HasCompileError() const {
        return compile_error_;
    }

    [[nodiscard]] const eastl::string& LastError() const {
        return last_error_;
    }

    //! Длительность последней успешной/неудачной компиляции (стена, включая симуляцию).
    [[nodiscard]] double LastCompileMs() const {
        return last_compile_ms_;
    }

    //! Вызов из кадрового цикла: имя известно на этапе компиляции, поиск по таблице функций
    //! кешируется до следующей перезагрузки. Пока идёт компиляция, вызов отклоняется: в
    //! контексте daScript в этот момент работает фоновый компилятор, а он не потокобезопасен.
    template <ScriptFunctionName Name, typename... Args>
    bool InvokeFunctionFast(Args&&... args) {
        if (!IsReady())
            return false;

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
        if (!IsReady())
            return false;

        auto* function = FindFunction<Name>();
        if (!function)
            return false;

        return InvokeSimFunctionSafe(function, Name.c_str(), std::forward<Args>(args)...);
    }

    //! Вызов с возвращаемым значением. Тип результата — второй template-параметр:
    //! `SimpleReturnUnsafe<"GetEditorViewportSize", das::float2>()`.
    template <ScriptFunctionName Name, typename T, typename... Args>
    Result<T> SimpleReturnUnsafe(Args&&... args) {
        // Callers of the render paths check only has_value(); the error is already reported
        // (loading is expected, a missing export is logged by FindFunction), so mark it
        // checked instead of tripping the debug "unchecked Error" assert on every frame.
        if (!IsReady()) {
            Error error(ErrorCode::EngineInternalError, eastl::string("scripts are not ready"));
            error.Discard();
            return std::unexpected(eastl::move(error));
        }

        auto* function = FindFunction<Name>();
        if (!function) {
            Error error(ErrorCode::EngineInternalError, eastl::string(Name.c_str()));
            error.Discard();
            return std::unexpected(eastl::move(error));
        }

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

    struct CompileTiming {
        double compile_ms = 0.0;
        double simulate_ms = 0.0;
        double wall_ms = 0.0;
    };

    //! Состояние одной фоновой компиляции. Живёт в shared_ptr: поток пула может закончить
    //! работу уже после того, как хост закрыл окно; деструктор ScriptSystem ждёт флаг done.
    struct AsyncCompileState;

    Result<CompiledScript> CompileMainScript(eastl::string_view path, bool use_module_cache,
                                             das::smart_ptr<das::FileAccess> file_access, CompileTiming& timing) const;
    void StartCompile(bool hot_reload, bool fresh_file_access);
    void InstallFirstLoad(CompiledScript&& compiled, eastl::string_view path, const CompileTiming& timing);
    void InstallReload(CompiledScript&& compiled, const CompileTiming& timing);
    void FailCompile(bool hot_reload, eastl::string_view message, const CompileTiming& timing);
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

    ScriptStatus status_ = ScriptStatus::Unloaded;
    bool compiling_ = false;
    bool compile_error_ = false;
    eastl::string last_error_;
    double last_compile_ms_ = 0.0;
    eastl::string pending_hook_errors_;
    std::shared_ptr<AsyncCompileState> async_;
};

}  // namespace tryengine::core
