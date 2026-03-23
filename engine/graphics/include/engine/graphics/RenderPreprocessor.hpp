#pragma once

namespace engine::graphics {
class RenderPreprocessor {
   public:
    void BuildView(entt::registry& reg) {
        Collect();
        FrustumCull();
        OcclusionCull();
        SelectLOD();
        Sort();
        Batch();
    }

   private:
    void Collect();
    void FrustumCull();
    void OcclusionCull();
    void SelectLOD();
    void Sort();
    void Batch();
};
}  // namespace engine::graphics
