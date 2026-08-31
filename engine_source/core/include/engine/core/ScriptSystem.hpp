#pragma once

#include <EASTL/functional.h>
#include <EASTL/string_view.h>
#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

#include "engine/core/Assert.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::core {

struct ScriptSystemConfig {
    eastl::string project_file = "project.das_project";
    eastl::function<void()> register_modules;
    eastl::function<das::Context*(uint32_t stack_size)> create_context;
};

class ScriptSystem {
public:
    explicit ScriptSystem(ScriptSystemConfig config);
    ~ScriptSystem();

    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    Result<void> LoadMainScript(eastl::string_view path);

    template <typename... Args>
    bool InvokeFunctionSafe(eastl::string_view f_name, Args&&... args) {
        if (!das_ctx_)
            return false;

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
            return LogAndMakeError("Function not found: {}", f_name.data());

        das::Func custom_func(function);
        return das::das_invoke_function<T>::invoke(das_ctx_, nullptr, custom_func, std::forward<Args>(args)...);
    }

    template <typename... Args>
    bool InvokeFunctionFast(eastl::string_view f_name, Args&&... args) {
        if (!das_ctx_)
            return false;

        auto* function = das_ctx_->findFunction(f_name.data());
        if (!function)
            return false;

        das::Func custom_func(function);
        das::das_invoke_function<void>::invoke(das_ctx_, nullptr, custom_func, std::forward<Args>(args)...);
        return true;
    }

    das::Context* GetContext() const { return das_ctx_; }

private:
    template <typename... Args>
    bool InvokeSimFunctionSafe(das::SimFunction* function, eastl::string_view f_name, Args&&... args) {
        if constexpr (sizeof...(Args) == 0) {
            das_ctx_->evalWithCatch(function, nullptr);
        } else {
            vec4f arguments[] = {das::cast<std::decay_t<Args>>::from(std::forward<Args>(args))...};
            das_ctx_->evalWithCatch(function, arguments);
        }

        if (auto ex = das_ctx_->getException()) {
            LogInfo(LogCategory::Script, "Скрипт упал в функции '{}': {}", f_name.data(), ex);
            return false;
        }

        return true;
    }

    ScriptSystemConfig config_;
    das::smart_ptr<das::FsFileAccess> file_access_;
    das::Context* das_ctx_ = nullptr;
};

}  // namespace tryengine::core