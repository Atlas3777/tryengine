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
    bool IsDescendantOf(entt::entity child, entt::entity parent, entt::registry& reg);
    entt::entity CloneEntity(entt::entity srcEntity, entt::registry& reg);

    void DestroyEntityRecursive(entt::entity entity, entt::registry& reg);

   private:
    // Состояние панели (UI State)
    std::vector<entt::entity> m_EntitiesToDestroy;
    std::vector<entt::entity> m_ClipboardEntities;

    entt::entity m_EntityToRename = entt::null;
    char m_RenameBuffer[256] = "";

    bool m_IsCutOperation = false;
};
}  // namespace tryeditor
