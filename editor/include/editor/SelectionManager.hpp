#pragma once
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <filesystem>
#include <variant>

#include "editor/Components.hpp"

namespace tryeditor {

enum class SelectionType : uint8_t { None, Entity, Asset };

class SelectionManager {
public:
    void Select(entt::entity entity, entt::registry& reg, bool additive = false) {
        ClearSelection(reg);
        active_entity = entity;
        type = SelectionType::Entity;
        // Можно сразу вешать SelectedTag здесь, чтобы панели не дублировали логику
        reg.emplace_or_replace<SelectedTag>(entity);
    }

    void Select(const std::filesystem::path& path, bool additive = false) {
        ClearSelection();
        active_asset_path = path;
        type = SelectionType::Asset;
    }

    void ClearSelection(entt::registry& reg) {
        if (type == SelectionType::Entity) {
            reg.clear<SelectedTag>();
        }
        active_entity = entt::null;
        active_asset_path.clear();
        type = SelectionType::None;
    }

    void ClearSelection() {
        active_entity = entt::null;
        active_asset_path.clear();
        type = SelectionType::None;
    }

    [[nodiscard]] SelectionType GetSelectionType() const { return type; }
    [[nodiscard]] entt::entity GetSelectedEntity() const { return active_entity; }
    const std::filesystem::path& GetSelectedAsset() const { return active_asset_path; }

private:
    SelectionType type = SelectionType::None;
    entt::entity active_entity = entt::null;
    std::filesystem::path active_asset_path;
};

}  // namespace tryeditor
