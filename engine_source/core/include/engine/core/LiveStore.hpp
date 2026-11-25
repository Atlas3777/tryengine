#pragma once

#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace tryengine::core {

//! Хранилище @live-переменных между поколениями скриптов: старый контекст пишет в
//! [before_reload], новый читает в [after_reload]. Живёт в процессе, а не в контексте,
//! поэтому переживает подмену поколения. Ключи и формат значения задаёт сгенерированный
//! макросом код (`tryengine/Live`); движок только держит байты.
void StoreLiveBytes(eastl::string_view key, const uint8_t* data, size_t size);
bool LoadLiveBytes(eastl::string_view key, eastl::vector<uint8_t>& data);

//! Новое поколение пришло из горячей перезагрузки; скрипты видят это как `is_reload()`.
void SetLiveReload(bool reload);
bool IsLiveReload();

//! Чистое поколение процесса: стирает всё, что могло остаться от прошлого прогона.
void ResetLiveStore();

}  // namespace tryengine::core
