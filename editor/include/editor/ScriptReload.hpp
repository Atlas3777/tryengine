#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <daScript/daScript.h>

#include "engine/core/ScriptSystem.hpp"

namespace tryeditor {

//! Горячая перезагрузка скриптов редактора.
//!
//! Движок даёт точку расширения (IScriptReloadExtension) и механику подмены контекста;
//! запрос из UI и повтор точек входа хоста живут здесь. Само состояние перезагрузки —
//! не редакторское: `@live`-переменные и мир возвращает live-хост SDK и `tryengine/LiveEcs`,
//! а редактор восстанавливает только игровой UI (см. ReloadSession.das). Здесь же ставятся
//! скриптовые флаги live-хоста: `is_reload`, `get_reload_generation`, `get_last_error`.
//! Игра расширение не подключает и о перезагрузке не знает.
class ScriptReload final : public tryengine::core::IScriptReloadExtension {
public:
    explicit ScriptReload(tryengine::core::ScriptSystem& script_system);

    ScriptReload(const ScriptReload&) = delete;
    ScriptReload& operator=(const ScriptReload&) = delete;

    // IScriptReloadExtension
    void ReinitializeContext(tryengine::core::ScriptSystem& system) override;
    void OnReloadFinished(bool success, eastl::string_view error) override;

    //! Запрос из кнопки/хоткея. Подмена контекста — только в кадровом цикле, когда ни одна
    //! функция скрипта не выполняется.
    void RequestReload();
    bool IsReloadRequested() const;
    void ProcessRequested();

    //! Короткий итог последней попытки. В интерфейсе не показывается (полный текст уже
    //! в логе); доступен скриптам, которым нужен статус.
    const eastl::string& Status() const {
        return status_;
    }

private:
    tryengine::core::ScriptSystem& script_system_;
    eastl::string status_;
    bool reload_requested_ = false;
};

//! Регистрирует в модуле TryEditor биндинги RequestScriptReload/ScriptReloadStatus. Держит
//! запрос перезагрузки из UI одним юнитом, а не набором глобальных функций в
//! Module_TryEditor.cpp.
void RegisterScriptReloadBindings(das::Module& module, das::ModuleLibrary& library);

}  // namespace tryeditor
