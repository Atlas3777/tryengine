#pragma once
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <iostream>

#include "IAssetInspector.hpp"
#include "editor/import/ImportSystem.hpp"
#include "editor/meta/TextureImportSettings.hpp"
#include "engine/resources/Types.hpp"
// Предположим, заголовок меты живет здесь
#include "editor/meta/AssetMetaHeader.hpp"

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

        // Кнопка растянута на всю ширину
        bool save_triggered = ImGui::Button("Save & Reimport", ImVec2(-1, 30));

        // Hotkey Ctrl+S
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_S)) {
            save_triggered = true;
        }

        if (save_triggered && is_asset_dirty_) {
            SaveAndReimport();
        }
    }

private:
    void LoadMeta(const std::filesystem::path& asset_path) {
        current_asset_path_ = asset_path;
        current_meta_path_ = asset_path.string() + ".meta";
        is_asset_dirty_ = false;

        if (std::filesystem::exists(current_meta_path_)) {
            try {
                std::ifstream is(current_meta_path_);
                cereal::JSONInputArchive archive(is);

                // Читаем раздельно, как в импортере
                archive(cereal::make_nvp("header", current_header_));
                archive(cereal::make_nvp("settings", current_settings_));

            } catch (const std::exception& e) {
                std::cerr << "[TextureAssetInspector] Ошибка чтения меты: " << e.what() << '\n';
            }
        }
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

    void SaveAndReimport() {
        // 1. Сохраняем обновленный .meta файл с раздельной структурой
        try {
            std::ofstream os(current_meta_path_);
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("header", current_header_));
            archive(cereal::make_nvp("settings", current_settings_));
        } catch (const std::exception& e) {
            std::cerr << "[TextureAssetInspector] Ошибка сохранения меты: " << e.what() << '\n';
            return;
        }

        // 2. Вызываем реимпорт через ImportSystem
        auto* importer = import_system_.GetImporterByName(current_header_.importer_type);
        if (importer) {
            // Пути должны соответствовать структуре вашего проекта
            std::filesystem::path root_path = std::filesystem::current_path() / "game";
            std::string guid_str = std::to_string(current_header_.guid);

            std::filesystem::path artefacts_dir = root_path / "artefacts" / guid_str;
            std::filesystem::path cache_dir = root_path / ".cache" / guid_str;
            std::filesystem::path assets_root = root_path / "assets";

            std::filesystem::create_directories(artefacts_dir);

            importer->GenerateArtifact(current_asset_path_, current_meta_path_, artefacts_dir, cache_dir, assets_root);
            std::cout << "[TextureAssetInspector] Texture reimported: " << guid_str << "\n";
        } else {
            std::cerr << "[TextureAssetInspector] Importer not found: " << current_header_.importer_type << '\n';
        }

        is_asset_dirty_ = false;
    }

    ImportSystem& import_system_;
    std::filesystem::path current_asset_path_;
    std::filesystem::path current_meta_path_;

    // Теперь данные разделены
    AssetMetaHeader current_header_;
    TextureImportSettings current_settings_;

    bool is_asset_dirty_ = false;
};

}  // namespace tryeditor