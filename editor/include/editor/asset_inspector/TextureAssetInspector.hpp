#pragma once
#include <imgui.h>

#include <filesystem>
#include <iostream>

#include "IAssetInspector.hpp"
#include "editor/import/ImportSystem.hpp"
#include "editor/import/TextureImporter.hpp"
#include "editor/meta/MetaSerializer.hpp"

namespace tryeditor {

class TextureAssetInspector : public IAssetInspector {
public:
    explicit TextureAssetInspector(ImportSystem& import_system) : import_system_(import_system) {}

    void DrawInspector(const std::filesystem::path& asset_path) override {
        if (current_asset_path_ != asset_path) {
            LoadMeta(asset_path);
        }

        DrawSettings();

        ImGui::Spacing();
        ImGui::Separator();

        if (is_asset_dirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Unsaved changes *");
        }

        bool save_triggered = ImGui::Button("Save & Reimport", ImVec2(-1, 30));

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_S)) {
            save_triggered = true;
        }

        if (save_triggered && is_asset_dirty_) {
            SaveAndReimport(asset_path);
        }
    }

private:
    void LoadMeta(const std::filesystem::path& asset_path) {
        current_asset_path_ = asset_path;
        current_meta_path_ = asset_path.string() + ".meta";

        if (!MetaSerializer::Read(current_meta_path_, current_header_, current_settings_)) {
            std::cerr << "[TextureAssetInspector] Ошибка загрузки .meta или настроек для " << asset_path << '\n';
        }

        is_asset_dirty_ = false;
    }

    void DrawSettings() {
        ImGui::Text("Texture Import Settings");
        // Вывод GUID для справки (Read Only)
        ImGui::TextDisabled("GUID: %llu", (unsigned long long) current_header_.guid);
        ImGui::Spacing();

        const char* filter_names[] = {"Nearest", "Linear"};
        const char* address_names[] = {"Repeat", "Mirrored Repeat", "Clamp To Edge"};

        using tryengine::resources::TextureAddressMode;
        using tryengine::resources::TextureFilter;

        // Min Filter
        int min_idx = static_cast<int>(current_settings_.min_filter);
        if (ImGui::Combo("Min Filter", &min_idx, filter_names, IM_ARRAYSIZE(filter_names))) {
            current_settings_.min_filter = static_cast<TextureFilter>(min_idx);
            is_asset_dirty_ = true;
        }

        // Mag Filter
        int mag_idx = static_cast<int>(current_settings_.mag_filter);
        if (ImGui::Combo("Mag Filter", &mag_idx, filter_names, IM_ARRAYSIZE(filter_names))) {
            current_settings_.mag_filter = static_cast<TextureFilter>(mag_idx);
            is_asset_dirty_ = true;
        }

        // Address U
        int u_idx = static_cast<int>(current_settings_.address_u);
        if (ImGui::Combo("Wrap U", &u_idx, address_names, IM_ARRAYSIZE(address_names))) {
            current_settings_.address_u = static_cast<TextureAddressMode>(u_idx);
            is_asset_dirty_ = true;
        }

        // Address V
        int v_idx = static_cast<int>(current_settings_.address_v);
        if (ImGui::Combo("Wrap V", &v_idx, address_names, IM_ARRAYSIZE(address_names))) {
            current_settings_.address_v = static_cast<TextureAddressMode>(v_idx);
            is_asset_dirty_ = true;
        }
    }

    void SaveAndReimport(const std::filesystem::path& asset_path) {
        // 1. Сохраняем ТОЛЬКО .meta файл с новыми настройками
        if (!MetaSerializer::Write(current_meta_path_, current_header_, current_settings_)) {
             std::cerr << "[TextureAssetInspector] Ошибка сохранения .meta\n";
             return;
        }

        import_system_.ReimportAsset(current_header_);

        is_asset_dirty_ = false;
    }

    ImportSystem& import_system_;
    std::filesystem::path current_asset_path_;
    std::filesystem::path current_meta_path_;

    AssetMetaHeader current_header_;
    TextureImportSettings current_settings_;

    bool is_asset_dirty_ = false;
};

} // namespace tryeditor