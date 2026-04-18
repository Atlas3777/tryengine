#pragma once

#include <imgui.h>

#include <entt/entity/entity.hpp>
#include <vector>

#include "IPanel.hpp"

namespace tryeditor {
class HierarchyPanel : public IPanel {
   public:
    const char* GetName() const override { return "Hierarchy"; }

    HierarchyPanel() = default;

    void OnImGuiRender(entt::registry& reg);

   private:
    void DrawEntityNode(entt::entity entity, entt::registry& reg);
    void HandleShortcuts(entt::registry& reg);

    void ReparentEntity(entt::entity child, entt::entity newParent, entt::registry& reg);
    entt::entity CloneEntity(entt::entity srcEntity, entt::registry& reg);

    void DestroyEntityRecursive(entt::entity entity, entt::registry& reg);

    // Состояние панели (UI State)
    std::vector<entt::entity> entities_to_destroy_;
    std::vector<entt::entity> clipboard_entities_;

    entt::entity entity_to_rename_ = entt::null;
    char rename_buffer_[256] = "";
    bool is_cut_operation_ = false;
};
}  // namespace tryeditor
