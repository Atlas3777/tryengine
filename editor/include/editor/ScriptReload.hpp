#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <daScript/daScript.h>

#include "engine/core/ScriptSystem.hpp"

namespace tryeditor {

//! Горячая перезагрузка скриптов редактора.
//!
//! Движок даёт только точку расширения (IScriptReloadExtension) и механику подмены
//! контекста; запрос из UI, снимок состояния и повтор точек входа хоста живут здесь.
//! Результат перезагрузки пишется в лог; отдельной строки статуса у редактора нет.
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

    //! Снимок между контекстами: пишет старый контекст, читает новый. Ключей нет — состояние
    //! одно и принадлежит редактору.
    void StoreBytes(const das::TArray<uint8_t>& data);
    bool LoadBytes(das::TArray<uint8_t>& data, das::Context* ctx);

private:
    tryengine::core::ScriptSystem& script_system_;
    eastl::vector<uint8_t> state_;
    eastl::string status_;
    bool reload_requested_ = false;
};

//! Регистрирует в модуле TryEditor аннотации before_reload/after_reload и биндинги
//! ScriptReload*. Держит hot reload одним юнитом, а не набором глобальных функций в
//! Module_TryEditor.cpp.
void RegisterScriptReloadBindings(das::Module& module, das::ModuleLibrary& library);

}  // namespace tryeditor
