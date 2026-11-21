#include "editor/ScriptReload.hpp"

#include <cstring>

#include "editor/TryEditorContext.hpp"
#include "engine/core/Log.hpp"

namespace tryeditor {

ScriptReload::ScriptReload(tryengine::core::ScriptSystem& script_system) : script_system_(script_system) {}

void ScriptReload::ReinitializeContext(tryengine::core::ScriptSystem& system) {
    // Новый контекст получает тот же старт, что и первый: корневой клок и InputSource из
    // EngineStart, подписки и editor-камеру из EditorStart. [after_reload] выполняется
    // уже после этого и возвращает мир.
    system.InvokeFunctionFast("EngineStart");
    system.InvokeFunctionFast("EditorStart");
}

void ScriptReload::OnReloadFinished(bool success, eastl::string_view error) {
    if (!success) {
        status_ = "Script reload failed:\n";
    } else if (!error.empty()) {
        status_ = "Scripts reloaded with hook errors:\n";
    } else {
        status_ = "Scripts reloaded";
        return;
    }
    status_.append(error.data(), error.size());
}

void ScriptReload::RequestReload() {
    reload_requested_ = true;
}

bool ScriptReload::IsReloadRequested() const {
    return reload_requested_;
}

void ScriptReload::ProcessRequested() {
    if (!reload_requested_)
        return;

    reload_requested_ = false;
    const auto result = script_system_.ReloadMainScript();
    if (!result.has_value())
        LogError(tryengine::core::LogCategory::Script, "Перезагрузка скриптов не удалась: {}", result.error());
}

void ScriptReload::StoreBytes(const das::TArray<uint8_t>& data) {
    state_.assign(data.data, data.data + data.size);
}

bool ScriptReload::LoadBytes(das::TArray<uint8_t>& data, das::Context* ctx) {
    if (state_.empty())
        return false;

    const uint32_t size = uint32_t(state_.size());
    das::array_resize(*ctx, data, size, sizeof(uint8_t), false, nullptr);
    if (size > 0)
        std::memcpy(data.data, state_.data(), size);
    return true;
}

namespace {

// Аннотации повторяют контракт daslang-live: имя функции становится префиксом, по которому
// ScriptSystem находит хуки. Живут в TryEditor, поэтому игра их не видит.

struct BeforeReloadAnnotation final : das::FunctionAnnotation {
    BeforeReloadAnnotation() : das::FunctionAnnotation("before_reload") {}

    bool apply(const das::FunctionPtr& func, das::ModuleGroup&, das::AnnotationArgumentList&, das::string&) override {
        func->exports = true;
        func->name = "__before_reload_" + func->name;
        return true;
    }

    bool apply(das::ExprBlock*, das::ModuleGroup&, das::AnnotationArgumentList&, das::string& err) override {
        err = "not supported for blocks";
        return false;
    }

    bool finalize(const das::FunctionPtr&, das::ModuleGroup&, das::AnnotationArgumentList&,
                  const das::AnnotationArgumentList&, das::string&) override {
        return true;
    }

    bool finalize(das::ExprBlock*, das::ModuleGroup&, das::AnnotationArgumentList&, const das::AnnotationArgumentList&,
                  das::string&) override {
        return true;
    }
};

struct AfterReloadAnnotation final : das::FunctionAnnotation {
    AfterReloadAnnotation() : das::FunctionAnnotation("after_reload") {}

    bool apply(const das::FunctionPtr& func, das::ModuleGroup&, das::AnnotationArgumentList&, das::string&) override {
        func->exports = true;
        func->name = "__after_reload_" + func->name;
        return true;
    }

    bool apply(das::ExprBlock*, das::ModuleGroup&, das::AnnotationArgumentList&, das::string& err) override {
        err = "not supported for blocks";
        return false;
    }

    bool finalize(const das::FunctionPtr&, das::ModuleGroup&, das::AnnotationArgumentList&,
                  const das::AnnotationArgumentList&, das::string&) override {
        return true;
    }

    bool finalize(das::ExprBlock*, das::ModuleGroup&, das::AnnotationArgumentList&, const das::AnnotationArgumentList&,
                  das::string&) override {
        return true;
    }
};

ScriptReload* FindScriptReload(das::Context* ctx) {
    auto* try_ctx = ctx ? static_cast<TryEditorContext*>(ctx) : nullptr;
    return try_ctx ? try_ctx->engine.TryGet<ScriptReload>() : nullptr;
}

void BindStoreBytes(const das::TArray<uint8_t>& data, das::Context* ctx) {
    if (auto* reload = FindScriptReload(ctx))
        reload->StoreBytes(data);
}

bool BindLoadBytes(das::TArray<uint8_t>& data, das::Context* ctx) {
    auto* reload = FindScriptReload(ctx);
    return reload ? reload->LoadBytes(data, ctx) : false;
}

bool BindRequestReload(das::Context* ctx) {
    auto* reload = FindScriptReload(ctx);
    if (!reload) {
        LogError(tryengine::core::LogCategory::Script, "RequestScriptReload: ScriptReload не зарегистрирован");
        return false;
    }
    reload->RequestReload();
    return true;
}

char* BindStatus(das::Context* ctx, das::LineInfoArg* at) {
    auto* reload = FindScriptReload(ctx);
    if (!reload)
        return ctx->allocateString("", 0, at);

    const auto& status = reload->Status();
    return ctx->allocateString(status.c_str(), uint32_t(status.size()), at);
}

}  // namespace

void RegisterScriptReloadBindings(das::Module& module, das::ModuleLibrary& library) {
    module.addAnnotation(new BeforeReloadAnnotation());
    module.addAnnotation(new AfterReloadAnnotation());

    das::addExtern<DAS_BIND_FUN(BindStoreBytes)>(module, library, "ScriptReloadStoreBytes",
                                                 das::SideEffects::modifyExternal, "BindStoreBytes");
    das::addExtern<DAS_BIND_FUN(BindLoadBytes)>(module, library, "ScriptReloadLoadBytes",
                                                das::SideEffects::modifyArgumentAndExternal, "BindLoadBytes");
    das::addExtern<DAS_BIND_FUN(BindRequestReload)>(module, library, "RequestScriptReload",
                                                    das::SideEffects::accessGlobal, "BindRequestReload");
    das::addExtern<DAS_BIND_FUN(BindStatus)>(module, library, "ScriptReloadStatus", das::SideEffects::accessGlobal,
                                             "BindStatus");
}

}  // namespace tryeditor
