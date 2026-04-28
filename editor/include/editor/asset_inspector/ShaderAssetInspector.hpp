#pragma once

#include "IAssetInspector.hpp"
#include "editor/EditorGUIUtils.hpp"
#include "editor/import/ImportSystem.hpp"
#include "editor/meta/MetaSerializer.hpp"

namespace tryeditor {

class ShaderAssetInspector : public IAssetInspector {
public:
    explicit ShaderAssetInspector(ImportSystem& import_system) : import_system_(import_system) {}

    void DrawInspector(const std::filesystem::path& asset_path) override {
        if (current_asset_path_ != asset_path) {
            current_asset_path_ = asset_path;
            import_system_.LoadNativeAsset(asset_path, current_shader_data_);
            dirty_ = false;
        }

        DrawShaderSlots();

        ImGui::Spacing();
        ImGui::Separator();

        if (dirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Unsaved changes *");
        }

        bool save_triggered = ImGui::Button("Save Asset", ImVec2(-1, 30));

        // Обработка Ctrl+S
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_S)) {
            save_triggered = true;
        }

        if (save_triggered && dirty_) {
            std::filesystem::path meta = current_asset_path_;
            auto header = MetaSerializer::ReadHeader(meta += ".meta");
            import_system_.SaveNativeAsset<tryengine::graphics::ShaderAsset>(
                current_asset_path_, current_shader_data_, *header);

            dirty_ = false;
        }
    }

private:
    void DrawShaderSlots() {
        if (ImGuiExt::AssetSlot("Vertex Shader:", current_shader_data_.vertex_shader_id,
                                tryengine::AssetType::GlslShader, "Vertex Shader (.vert)")) {
            dirty_ = true;
        }

        ImGui::Spacing();

        if (ImGuiExt::AssetSlot("Fragment Shader:", current_shader_data_.fragment_shader_id,
                                tryengine::AssetType::GlslShader, "Fragment Shader (.frag)")) {
            dirty_ = true;
        }

        ImGui::SeparatorText("Uniform Parameters");
        ImGui::PushID("ParamsList");
        for (int i = 0; i < (int)current_shader_data_.params.size(); ++i) {
            ImGui::PushID(i);
            auto& p = current_shader_data_.params[i];

            if (ImGui::Button("X")) {
                current_shader_data_.params.erase(current_shader_data_.params.begin() + i);
                dirty_ = true;
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();

            char buf[64];
            strcpy(buf, p.name.c_str());
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText("##name", buf, 64)) {
                p.name = buf;
                dirty_ = true;
            }
            ImGui::SameLine();

            int type_idx = (int)p.type;
            const char* types[] = {"Float", "Int", "Vec2", "Vec3", "Vec4", "Mat3", "Mat4"};
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::Combo("##type", &type_idx, types, 7)) {
                p.type = (tryengine::graphics::ShaderParamType)type_idx;
                dirty_ = true;
            }

            ImGui::PopID();
        }

        if (ImGui::Button("Add Parameter")) {
            current_shader_data_.params.push_back({"u_NewParam", tryengine::graphics::ShaderParamType::Float, {0.0f}});
            dirty_ = true;
        }
        ImGui::PopID();

        ImGui::Spacing();

        ImGui::SeparatorText("Texture Slots");
        ImGui::PushID("TexturesList");
        for (int i = 0; i < (int)current_shader_data_.textures.size(); ++i) {
            ImGui::PushID(i);
            auto& t = current_shader_data_.textures[i];

            if (ImGui::Button("X")) {
                current_shader_data_.textures.erase(current_shader_data_.textures.begin() + i);
                dirty_ = true;
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();

            char buf[64];
            strcpy(buf, t.name.c_str());
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText("##tex_name", buf, 64)) {
                t.name = buf;
                dirty_ = true;
            }
            ImGui::SameLine();

            ImGui::Text("Slot");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            int slot_val = (int)t.slot;
            if (ImGui::InputInt("##tex_slot", &slot_val, 1, 1)) {
                t.slot = (uint32_t)std::max(0, slot_val);
                dirty_ = true;
            }

            ImGui::PopID();
        }

        if (ImGui::Button("Add Texture Slot")) {
            uint32_t next_slot = 0;
            for (const auto& t : current_shader_data_.textures) {
                if (t.slot >= next_slot) {
                    next_slot = t.slot + 1;
                }
            }
            current_shader_data_.textures.push_back({"albedo_map", next_slot});
            dirty_ = true;
        }
        ImGui::PopID();
    }

    ImportSystem& import_system_;
    std::filesystem::path current_asset_path_;
    tryengine::graphics::ShaderAsset current_shader_data_;
    bool dirty_ = false;
};

}  // namespace tryeditor