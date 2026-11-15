# engine_source/graphics — заметки для агентов

Дополнение к корневому `AGENTS.md`. Здесь только неочевидные вещи этого каталога.

## Математика — типы и операции daScript

Рендер-структуры используют родные типы daScript: `das::float3`, `das::float4`,
`das::float4x4` (`<daScript/misc/vectypes.h>`, `<daScript/simulate/runtime_matrices.h>`).
Сторонние math-библиотеки в этот слой не добавляем: типы должны совпадать по памяти
с тем, что приходит из скриптов.

| тип | size | align |
| --- | --- | --- |
| `das::float3` | 12 | 4 |
| `das::float4` | 16 | 4 |
| `das::float4x4` | 64 | 4 |

Готовые операции берём из daScript, а не пишем свои:

- `das::float4x4_inverse`, `das::float4x4_transpose`, `das::float4x4_compose`,
  `das::quat_from_float3x3`, `das::quat_from_float4x4` — объявления в
  `<daScript/simulate/aot_builtin_matrix.h>`;
- линкуются через `engine::core` → `DAS::libDaScriptDyn`, отдельная зависимость не нужна;
- семантика совпадает со скриптовыми `inverse()`, `transpose()`, `compose()`.

Матрицы — row-vector: трансляция лежит в `m[3]`, `compose(pos, quat, scale)` кладёт её туда
же. Шейдеры ожидают тот же порядок, не переставляй строки и столбцы.

## Выравнивание — главная ловушка

У `das::float4x4` `alignof == 4`, а не 16. Поэтому:

- структуры, которые уходят в GPU, помечай `alignas(16)` явно (`GPUObjectData`,
  `CameraGPU`, `PointLightGPU`, `FragmentDataGPU`);
- зеркала daslang-структур с `[cpp_layout]` проверяй `static_assert` по размеру: без
  явного `_pad` размер может не совпасть из-за хвостового выравнивания
  (пример — `DaslangDrawCall` = 96 байт);
- размер на стороне скрипта можно проверить так: `var x : Type;` и
  `print("{typeinfo sizeof(x)}")`.

## Мост со скриптами

- `engine_source/daslang/Render.das` описывает `[cpp_layout] struct DrawCall` и
  `struct CameraC`. C++-зеркала: `DaslangDrawCall` в `OpaquePass.cpp` и `CameraData`
  в `RenderCommon.hpp` (`CameraC` = 140 байт). Меняешь поля в `.das` — обнови зеркало
  и `static_assert`.
- `get_render_obj` и `get_lights` возвращают `das::Array*`; элементы читаются приведением
  указателя, поэтому важна раскладка, а не имена полей.
- `GetEditorCamera`/`GetGameCamera` возвращают указатель на `CameraC` (view, proj, position);
  C++ копирует его в `CameraData`.
- `normalMatrix` считается в `OpaquePass` как `transpose(inverse(model))` от daScript-функций.
  Если переносишь расчёт в скрипт, формула должна остаться той же.

## Логи

Для `das::float3`/`das::float4`/`das::float4x4` нет `std::formatter`, поэтому
`LogInfo("{}", vec)` не соберётся. Разворачивай вектор в компоненты
(`v.x, v.y, v.z`) либо добавь форматтер осознанно.

## Материалы

- `Material` создаёт GPU-ресурсы отдельным вызовом `InitGpuResources` (uniform buffer
  мапится, descriptor set (set 1) аллоцируется). `SetParamRaw` пишет значение прямо в
  замапленную память, поэтому загрузчик обязан вызвать `InitGpuResources` **до** установки
  параметров — иначе они молча теряются (именно так материал однажды отрисовался с нулевым
  albedo).
- Раскладка UBO берётся из рефлексии Slang; в `.mat` поле `type` — тип хранения
  (`u64` у текстур), а `kind` — `uniform` или `texture`. C++-сторона — `MaterialConfig`,
  редакторская — `editor/daslang/Material.das`.
- Один material UBO на сет 1 работает; несколько UBO в сете 1 пока разделяют один буфер
  (дескриптор у каждого смотрит в offset 0) — это стоит поправить до появления второго
  cbuffer'а в шейдере.

## Биндинги daslang

- По файлу на нативный модуль, и файл лежит в слое, чьи типы он отдаёт в скрипт: здесь
  `src/Module_Renderer.cpp` (`tryRenderer`) и `src/Module_Resources.cpp` (`tryResources`,
  ему нужны `Mesh`/`Material`/`Shader`). `tryInput` живёт в `engine_source/platform`,
  `tryAsyncFile` — в `engine_source/resources`: их биндинги не должны переезжать сюда
  только потому, что тут когда-то лежал общий файл.
- Общий файл на несколько модулей не собирай: каждый модуль — своя shared-библиотека
  `modules/<имя>/` (см. `tryengine_add_das_module()` в `cmake/DasModule.cmake`).
- Эти же `.cpp` попадают в статическую `engine_graphics` (glob `src/*.cpp`), поэтому
  редактор и игра получают модули через `PULL_MODULE`, без загрузки dll.
- `REGISTER_MODULE`/`REGISTER_DYN_MODULE` и `MAKE_TYPE_FACTORY` живут вместе с модулем;
  `DAS_BASE_BIND_ENUM` по-прежнему обязан идти до `using namespace das`.
