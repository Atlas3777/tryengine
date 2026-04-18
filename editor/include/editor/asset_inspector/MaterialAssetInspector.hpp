#pragma once
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <fstream>
#include <iostream>
#include <map>

#include "IAssetInspector.hpp"
#include "editor/EditorGUIUtils.hpp"


namespace tryeditor {

class MaterialAssetInspector : public IAssetInspector {
public:
    void DrawInspector(const std::filesystem::path& asset_path) override {
        // Ленивая загрузка ассета
        if (current_asset_path_ != asset_path) {
            LoadAsset(asset_path);
        }

        DrawMaterialSlots();

        ImGui::Spacing();
        ImGui::Separator();

        // 1. Индикация несохраненных изменений
        if (is_asset_dirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Unsaved changes *");
        }

        // 2. Кнопка сохранения и проверка хоткея (Ctrl + S)
        bool save_triggered = ImGui::Button("Save Asset", ImVec2(-1, 30));

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_S)) {
            save_triggered = true;
        }

        if (save_triggered && is_asset_dirty_) {
            SaveAsset(asset_path);
        }
    }

private:
    void DrawMaterialSlots() {
        // ==========================================
        // Шейдер материала
        // ==========================================
        if (ImGuiExt::AssetSlot("Shader:", current_material_def_.shader_asset_id, tryengine::AssetType::Shader,
                                "Shader Asset (.shader)")) {
            is_asset_dirty_ = true;
        }

        ImGui::Spacing();

        // Переменные для отложенного удаления и переименования ключей в std::map
        std::string param_to_remove;
        std::pair<std::string, std::string> param_to_rename;

        std::string tex_to_remove;
        std::pair<std::string, std::string> tex_to_rename;

        // ==========================================
        // Блок Скалярных параметров (Uniforms)
        // ==========================================
        ImGui::SeparatorText("Scalar Parameters");
        ImGui::PushID("ScalarParamsList");

        for (auto& [name, values] : current_material_def_.scalar_params) {
            ImGui::PushID(name.c_str());

            if (ImGui::Button("X")) {
                param_to_remove = name;
                is_asset_dirty_ = true;
            }
            ImGui::SameLine();

            // Имя параметра
            char buf[64];
            strcpy(buf, name.c_str());
            ImGui::SetNextItemWidth(150.0f);
            // Используем EnterReturnsTrue или DeactivatedAfterEdit, чтобы не переименовывать на каждый символ (это ломает map)
            if (ImGui::InputText("##name", buf, 64, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_NoUndoRedo)) {
                if (name != buf) {
                    param_to_rename = {name, buf};
                    is_asset_dirty_ = true;
                }
            }
            ImGui::SameLine();

            // Редактирование значений вектора
            int vec_size = (int)values.size();
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::SliderInt("Size", &vec_size, 1, 16)) {
                values.resize(vec_size, 0.0f);
                is_asset_dirty_ = true;
            }
            ImGui::SameLine();

            // Рисуем float инпуты для каждого элемента (в одну линию или с переносом)
            ImGui::PushItemWidth(60.0f);
            for (size_t i = 0; i < values.size(); ++i) {
                ImGui::PushID((int)i);
                if (ImGui::DragFloat("##val", &values[i], 0.01f)) {
                    is_asset_dirty_ = true;
                }
                if (i < values.size() - 1) ImGui::SameLine();
                ImGui::PopID();
            }
            ImGui::PopItemWidth();

            ImGui::PopID();
        }

        // Применяем отложенные операции для параметров
        if (!param_to_remove.empty()) {
            current_material_def_.scalar_params.erase(param_to_remove);
        }
        if (!param_to_rename.first.empty() && !param_to_rename.second.empty()) {
            // C++17 node extraction: позволяет поменять ключ без реаллокации
            auto node = current_material_def_.scalar_params.extract(param_to_rename.first);
            if (!node.empty()) {
                node.key() = param_to_rename.second;
                current_material_def_.scalar_params.insert(std::move(node));
            }
        }

        if (ImGui::Button("Add Parameter")) {
            current_material_def_.scalar_params["u_NewParam"] = {0.0f, 0.0f, 0.0f, 0.0f}; // По умолчанию Vec4
            is_asset_dirty_ = true;
        }
        ImGui::PopID(); // ScalarParamsList

        ImGui::Spacing();

        // ==========================================
        // Блок Текстурных параметров
        // ==========================================
        ImGui::SeparatorText("Texture Parameters");
        ImGui::PushID("TexturesList");

        for (auto& [name, tex_id] : current_material_def_.texture_params) {
            ImGui::PushID(name.c_str());

            // Кнопка удаления
            if (ImGui::Button("X")) {
                tex_to_remove = name;
                is_asset_dirty_ = true;
            }
            ImGui::SameLine();

            // Имя текстурного юниформа (например: "u_AlbedoMap")
            char buf[64];
            strcpy(buf, name.c_str());
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText("##tex_name", buf, 64, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_NoUndoRedo)) {
                if (name != buf) {
                    tex_to_rename = {name, buf};
                    is_asset_dirty_ = true;
                }
            }
            ImGui::SameLine();

            // Слот для текстуры
            if (ImGuiExt::AssetSlot("##texture", tex_id, tryengine::AssetType::Texture, "Texture Asset")) {
                is_asset_dirty_ = true;
            }

            ImGui::PopID();
        }

        // Применяем отложенные операции для текстур
        if (!tex_to_remove.empty()) {
            current_material_def_.texture_params.erase(tex_to_remove);
        }
        if (!tex_to_rename.first.empty() && !tex_to_rename.second.empty()) {
            auto node = current_material_def_.texture_params.extract(tex_to_rename.first);
            if (!node.empty()) {
                node.key() = tex_to_rename.second;
                current_material_def_.texture_params.insert(std::move(node));
            }
        }

        if (ImGui::Button("Add Texture")) {
            current_material_def_.texture_params["u_NewTexture"] = 0; // ID 0 по умолчанию
            is_asset_dirty_ = true;
        }
        ImGui::PopID(); // TexturesList
    }

    void LoadAsset(const std::filesystem::path& path) {
        current_asset_path_ = path;
        std::ifstream is(path);
        if (is.is_open()) {
            try {
                cereal::JSONInputArchive archive(is);
                archive(current_material_def_);
            } catch (const std::exception& e) {
                std::cerr << "Material Inspector Load Error: " << e.what() << std::endl;
            }
        }
        is_asset_dirty_ = false;
    }

    void SaveAsset(const std::filesystem::path& path) {
        // Шаг 1: Сохраняем ИСХОДНИК (.mat или .material в JSON формате)
        {
            std::ofstream os(path);
            if (os.is_open()) {
                cereal::JSONOutputArchive archive(os);
                archive(cereal::make_nvp("material_def", current_material_def_));
            }
        }

        // Шаг 2: Читаем .meta файл, чтобы достать GUID
        std::filesystem::path meta_path = path.string() + ".meta";
        uint64_t guid = 0;

        {
            std::ifstream meta_is(meta_path);
            if (meta_is.is_open()) {
                try {
                    cereal::JSONInputArchive meta_archive(meta_is);
                    AssetMetaHeader header;
                    meta_archive(cereal::make_nvp("header", header));
                    guid = header.guid;
                } catch (const std::exception& e) {
                    std::cerr << "Meta File Read Error: " << e.what() << std::endl;
                }
            }
        }

        // Шаг 3: Перезаписываем АРТЕФАКТ (бинарник в папке artefacts)
        if (guid != 0) {
            std::filesystem::path artefacts_dir = std::filesystem::current_path() / "game" / "artefacts";
            std::filesystem::path folder = artefacts_dir / std::to_string(guid);

            std::filesystem::create_directories(folder);

            // Используем расширение .mat (или любое другое, которое у тебя предназначено для скомпилированных материалов)
            std::filesystem::path artefact_path = folder / (std::to_string(guid) + ".mat");

            std::ofstream bin_os(artefact_path, std::ios::binary);
            if (bin_os.is_open()) {
                cereal::BinaryOutputArchive bin_archive(bin_os);
                bin_archive(current_material_def_);
            } else {
                std::cerr << "Failed to write artifact at: " << artefact_path << std::endl;
            }
        }

        is_asset_dirty_ = false;
    }

    std::filesystem::path current_asset_path_;
    tryengine::resources::MaterialAssetData current_material_def_;
    bool is_asset_dirty_ = false;
};

}  // namespace tryeditor