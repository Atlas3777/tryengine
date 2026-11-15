options gen2
options relaxed_assign = false

module PickerTest shared public

// Тестовый компонент для проверки asset picker'а в инспекторе.
// Добавляется через "Add component" -> PickerDemo; поле material должно
// рисоваться с кнопкой выбора материала.

require tryengine/Types
require tryengine/EcsMacro
require tryengine/Serializable
require tryengine/ConfigFormatBoost
require daslib/decs

[DecsSerializable]
struct PickerDemo {
    @asset = "material"
    material : string
}

%ecs~("PickerDemo", type<PickerDemo>) %%
