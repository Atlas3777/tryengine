#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <daScript/daScript.h>

namespace tryengine::core {

class ScriptSystem {
public:
    ScriptSystem();
    ~ScriptSystem();

    // Прежний интерфейс для main-скрипта (для обратной совместимости)
    bool LoadMainScript(const std::string& path);
    void InvokeStart();
    void InvokeUpdate(float dt);
    das::Context* GetContext();

    // --- НОВЫЙ ИНТЕРФЕЙС ДЛЯ КАСТОМНЫХ СКРИПТОВ ---
    bool LoadCustomScript(const std::string& id, const std::string& path);

    // Универсальный вызов функции без параметров
    void InvokeCustomFunction(const std::string& id, const std::string& function_name);

    // Шаблонный вызов функции для передачи аргументов (например, если нужно передать dt или указатели)
    template<typename... Args>
    void InvokeCustomFunctionArgs(const std::string& id, const std::string& function_name, Args&&... args) {
        auto it = custom_scripts_.find(id);
        if (it != custom_scripts_.end() && it->second->context) {
            auto* ctx = it->second->context;
            if (auto* fn = ctx->findFunction(function_name.c_str())) {
                das::Func func(fn);
                das::das_invoke_function<void>::invoke(ctx, nullptr, func, std::forward<Args>(args)...);
            }
        }
    }

private:
    struct ScriptInstance {
        das::Context* context = nullptr;
        ~ScriptInstance() {
            if (context) {
                delete context;
            }
        }
    };

    // Данные основного скрипта
    das::Context* das_ctx = nullptr;
    das::SimFunction* fn_start = nullptr;
    das::SimFunction* fn_update = nullptr;

    // Хранилище кастомных скриптов (например, для эдитора)
    std::unordered_map<std::string, std::unique_ptr<ScriptInstance>> custom_scripts_;
};

} // namespace tryengine::core