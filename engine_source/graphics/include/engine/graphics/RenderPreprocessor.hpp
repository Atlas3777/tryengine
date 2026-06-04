#pragma once

namespace tryengine::graphics {
class RenderPreprocessor {
   public:
    void BuildView() {
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
}  // namespace tryengine::graphics
