#include "editor/ScriptReload.hpp"

#include "editor/TryEditorContext.hpp"
#include "engine/core/Log.hpp"

namespace tryeditor {

ScriptReload::ScriptReload(tryengine::core::ScriptSystem& script_system) : script_system_(script_system) {}

void ScriptReload::ReinitializeContext(tryengine::core::ScriptSystem& system) {
    // Новый контекст получает тот же старт, что и первый: корневой клок и InputSource из
    // EngineStart, подписки и editor-камеру из EditorStart. Состояние возвращают @live-хуки
    // движка и координатор перезагрузки, который выполняется уже после этого.
    system.InvokeFunctionFast<"EngineStart">();
    system.InvokeFunctionFast<"EditorStart">();
}

void ScriptReload::OnReloadFinished(bool success, eastl::string_view error) {
    // Полный текст ошибки уже в логе; статус короткий и предназначен для скриптов.
    if (!success)
        status_ = "reload failed (see log)";
    else if (!error.empty())
        status_ = "reloaded with hook errors (see log)";
    else
        status_ = "reloaded";
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
    // Компиляция уходит на пул потоков: кадр не ждёт JIT. Пока она идёт, ScriptSystem
    // отклоняет вызовы скриптов, а PollAsync на главном потоке подменит контекст или
    // оставит старое поколение при ошибке.
    script_system_.ReloadMainScriptAsync();
}

namespace {

ScriptReload* FindScriptReload(das::Context* ctx) {
    auto* try_ctx = ctx ? static_cast<TryEditorContext*>(ctx) : nullptr;
    return try_ctx ? try_ctx->engine.TryGet<ScriptReload>() : nullptr;
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
    // Аннотации before_reload/after_reload регистрирует tryLive: это контракт движка, а не
    // редактора. Здесь остаётся только запрос перезагрузки из UI и короткий статус.
    das::addExtern<DAS_BIND_FUN(BindRequestReload)>(module, library, "RequestScriptReload",
                                                    das::SideEffects::accessGlobal, "BindRequestReload");
    das::addExtern<DAS_BIND_FUN(BindStatus)>(module, library, "ScriptReloadStatus", das::SideEffects::accessGlobal,
                                             "BindStatus");
}

}  // namespace tryeditor
