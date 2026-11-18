#pragma once

namespace tryengine {

enum class EditorArtifactGuid {
    AssetMap = 10,
    // Плоская pre-order композиция модели в формате .prefab. Только редактор: игра
    // спавнит префабы, а не glb, и этот артефакт в пак не попадает.
    Prefab = 11,
};
}  // namespace tryengine