#pragma once
#include <cstdint>
#include <cstring>
#include <imgui.h>
#include <string>

#include "engine/core/CoreTypes.hpp"
#include "engine/resources/AssetTypes.hpp"
#include "engine/resources/MaterialAssetData.hpp"

namespace tryeditor {

// То, что мы передаем при Drag-and-Drop
struct AssetPayload {
    uint64_t asset_id;
    tryengine::AssetTypeID expected_asset_type;
};

namespace ImGuiExt {

inline bool AssetSlot(const char* label, uint64_t& asset_id, tryengine::AssetTypeID expected_asset_type,
                      const char* visual_hint = "Asset") {
    bool changed = false;

    ImGui::Text("%s", label);
    ImGui::SameLine();

    std::string btn_label =
        asset_id == 0 ? std::string("[ Drop ") + visual_hint + " ]" : "Asset ID: " + std::to_string(asset_id);

    ImGui::Button(btn_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0));

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            const auto* data = static_cast<const AssetPayload*>(payload->Data);

            // Быстрое сравнение двух uint32_t
            if (expected_asset_type == data->expected_asset_type) {
                if (payload->IsDelivery()) {
                    asset_id = data->asset_id;
                    changed = true;
                }
            } else {
                ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
            }
        }
        ImGui::EndDragDropTarget();
    }

    return changed;
}

}  // namespace ImGuiExt
}  // namespace tryeditor