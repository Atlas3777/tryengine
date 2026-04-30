#pragma once

#include "editor/asset_inspector/IAssetInspector.hpp"
#include "editor/EditorGUIUtils.hpp"
#include "editor/import/ImportSystem.hpp"

namespace tryeditor {

class MaterialAssetInspector : public IAssetInspector {
public:
    explicit MaterialAssetInspector(ImportSystem& import_system) : import_system_(import_system) {}

    void DrawInspector(const std::filesystem::path& asset_path) override {
        if (current_asset_path_ != asset_path) {
            current_asset_path_ = asset_path;
            import_system_.LoadNativeAsset(asset_path, current_material_data_);
            dirty_ = false;
        }

        DrawMaterialSlots();

        ImGui::Spacing();
        ImGui::Separator();

        if (dirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Unsaved changes *");
        }

        bool save_triggered = ImGui::Button("Save Asset", ImVec2(-1, 30));

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_S)) {
            save_triggered = true;
        }

        if (save_triggered && dirty_) {
            std::filesystem::path meta = current_asset_path_;
            auto header = MetaSerializer::ReadHeader(meta += ".meta");
            import_system_.SaveNativeAsset<tryengine::resources::MaterialAssetData>(current_asset_path_, current_material_data_, *header);
            dirty_ = false;
        }
    }

private:
    void DrawMaterialSlots() {
        if (ImGuiExt::AssetSlot("Shader:", current_material_data_.shader_asset_id, tryengine::AssetType::Shader,
                                "Shader Asset (.shader)")) {
            dirty_ = true;
        }

        ImGui::Spacing();

        std::string param_to_remove;
        std::pair<std::string, std::string> param_to_rename;

        std::string tex_to_remove;
        std::pair<std::string, std::string> tex_to_rename;

        ImGui::SeparatorText("Scalar Parameters");
        ImGui::PushID("ScalarParamsList");

        for (auto& [name, values] : current_material_data_.scalar_params) {
            ImGui::PushID(name.c_str());

            if (ImGui::Button("X")) {
                param_to_remove = name;
                dirty_ = true;
            }
            ImGui::SameLine();

            char buf[64];
            strcpy(buf, name.c_str());
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText("##name", buf, 64,
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_NoUndoRedo)) {
                if (name != buf) {
                    param_to_rename = {name, buf};
                    dirty_ = true;
                }
            }
            ImGui::SameLine();

            int vec_size = (int) values.size();
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::SliderInt("Size", &vec_size, 1, 16)) {
                values.resize(vec_size, 0.0f);
                dirty_ = true;
            }
            ImGui::SameLine();

            ImGui::PushItemWidth(60.0f);
            for (size_t i = 0; i < values.size(); ++i) {
                ImGui::PushID((int) i);
                if (ImGui::DragFloat("##val", &values[i], 0.01f)) {
                    dirty_ = true;
                }
                if (i < values.size() - 1)
                    ImGui::SameLine();
                ImGui::PopID();
            }
            ImGui::PopItemWidth();

            ImGui::PopID();
        }

        if (!param_to_remove.empty()) {
            current_material_data_.scalar_params.erase(param_to_remove);
        }
        if (!param_to_rename.first.empty() && !param_to_rename.second.empty()) {
            auto node = current_material_data_.scalar_params.extract(param_to_rename.first);
            if (!node.empty()) {
                node.key() = param_to_rename.second;
                current_material_data_.scalar_params.insert(std::move(node));
            }
        }

        if (ImGui::Button("Add Parameter")) {
            current_material_data_.scalar_params["u_NewParam"] = {0.0f, 0.0f, 0.0f, 0.0f};
            dirty_ = true;
        }
        ImGui::PopID();

        ImGui::Spacing();

        ImGui::SeparatorText("Texture Parameters");
        ImGui::PushID("TexturesList");

        for (auto& [name, tex_id] : current_material_data_.texture_params) {
            ImGui::PushID(name.c_str());

            if (ImGui::Button("X")) {
                tex_to_remove = name;
                dirty_ = true;
            }
            ImGui::SameLine();

            char buf[64];
            strcpy(buf, name.c_str());
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText("##tex_name", buf, 64,
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_NoUndoRedo)) {
                if (name != buf) {
                    tex_to_rename = {name, buf};
                    dirty_ = true;
                }
            }
            ImGui::SameLine();

            if (ImGuiExt::AssetSlot("##texture", tex_id, tryengine::AssetType::Texture, "Texture Asset")) {
                dirty_ = true;
            }

            ImGui::PopID();
        }

        if (!tex_to_remove.empty()) {
            current_material_data_.texture_params.erase(tex_to_remove);
        }
        if (!tex_to_rename.first.empty() && !tex_to_rename.second.empty()) {
            auto node = current_material_data_.texture_params.extract(tex_to_rename.first);
            if (!node.empty()) {
                node.key() = tex_to_rename.second;
                current_material_data_.texture_params.insert(std::move(node));
            }
        }

        if (ImGui::Button("Add Texture")) {
            current_material_data_.texture_params["u_NewTexture"] = 0;
            dirty_ = true;
        }
        ImGui::PopID();
    }

    ImportSystem& import_system_;
    std::filesystem::path current_asset_path_;
    tryengine::resources::MaterialAssetData current_material_data_;
    bool dirty_ = false;
};

}  // namespace tryeditor