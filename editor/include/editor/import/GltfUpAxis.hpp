#pragma once

#include <EASTL/string_view.h>
#include <cstdint>

#include "engine/graphics/Transform.hpp"

namespace tryeditor {

//! Ось исходного файла, которая смотрит вверх. glTF по спецификации Y-up, а движок Z-up
//! (гравитация −Z, камера смотрит вдоль +Y и держит +Z вверх), поэтому модель, импортированная
//! как есть, лежит на боку. `settings.up_axis` в .meta называет ось исходника, а импортёр
//! поворачивает корни сцены так, чтобы она совпала с +Z движка.
//!
//! Значения попали в `.meta` строкой (ConfigFormat пишет enum именем), поэтому порядок и
//! имена — контракт файла: `GltfUpAxis::Y` = "Y".
enum class GltfUpAxis : uint32_t {
    Y = 0,  //!< норма glTF: +90° вокруг X, Y -> Z
    Z = 1,  //!< файл уже в Z-up движка: поворот не нужен
    X = 2,  //!< −90° вокруг Y, X -> Z
};

//! Имя оси так, как оно пишется в .meta и показывается в панели. Совпадает с
//! `string(GltfUpAxis)` в daslang: форма редактора и кодек обязаны писать одну строку.
eastl::string_view GltfUpAxisName(GltfUpAxis axis);

//! Обратный разбор. Принимает и строчные имена: .meta правят руками, и `y` там стоит с
//! прошлой версии файла. false — имя не знакомо, импорт обязан упасть.
bool GltfUpAxisFromName(eastl::string_view name, GltfUpAxis& out);

//! Переводит локальный трансформ в систему движка: `world = R * local`, то есть ровно то, что
//! `TransformSystem::ComposeWorld` делает с ребёнком повёрнутого родителя. Позиция
//! поворачивается, кватернион домножается слева и нормализуется, масштаб не меняется.
//! Применять только к корням сцены: локальные трансформы детей уже в системе координат glTF,
//! и поворот над корнем вращает всё поддерево сразу.
void ConvertGltfTransformToEngineUp(GltfUpAxis axis, tryengine::Transform& transform);

}  // namespace tryeditor
