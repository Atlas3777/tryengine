#pragma once
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <fstream>
#include <iostream>

#include "IAssetInspector.hpp"
#include "editor/EditorGUIUtils.hpp"

namespace tryeditor {

class ShaderAssetInspector : public IAssetInspector {
public:
    void DrawInspector(const std::filesystem::path& asset_path) override {
        // Ленивая загрузка ассета при переключении выбора
        if (current_asset_path_ != asset_path) {
            LoadAsset(asset_path);
        }

        DrawShaderSlots();

        ImGui::Spacing();
        ImGui::Separator();

        // 1. Визуальная индикация несохраненных изменений
        if (is_asset_dirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Unsaved changes *");
        }

        // 2. Кнопка сохранения и проверка хоткея (Ctrl + S)
        bool save_triggered = ImGui::Button("Save Asset", ImVec2(-1, 30));

        // Проверяем нажатие Ctrl+S, только если окно инспектора в фокусе
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_S)) {
            save_triggered = true;
        }

        if (save_triggered && is_asset_dirty_) {
            SaveAsset(asset_path);
        }
    }

private:
    void DrawShaderSlots() {
        if (ImGuiExt::AssetSlot("Vertex Shader:", current_shader_def_.vertex_shader_id, tryengine::AssetType::GlslShader,
                                "Vertex Shader (.vert)")) {
            is_asset_dirty_ = true;
        }

        ImGui::Spacing();

        if (ImGuiExt::AssetSlot("Fragment Shader:", current_shader_def_.fragment_shader_id,
                                tryengine::AssetType::GlslShader, "Fragment Shader (.frag)")) {
            is_asset_dirty_ = true;
        }

        // ==========================================
        // Блок Uniform-параметров
        // ==========================================
        ImGui::SeparatorText("Uniform Parameters");
        ImGui::PushID("ParamsList");
        for (int i = 0; i < (int) current_shader_def_.params.size(); ++i) {
            ImGui::PushID(i);
            auto& p = current_shader_def_.params[i];

            if (ImGui::Button("X")) {
                current_shader_def_.params.erase(current_shader_def_.params.begin() + i);
                is_asset_dirty_ = true;
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();

            char buf[64];
            strcpy(buf, p.name.c_str());
            ImGui::SetNextItemWidth(150.0f); // Ограничиваем ширину, чтобы влезло в одну строку
            if (ImGui::InputText("##name", buf, 64)) {
                p.name = buf;
                is_asset_dirty_ = true;
            }
            ImGui::SameLine();

            int type_idx = (int) p.type;
            const char* types[] = {"Float", "Int", "Vec2", "Vec3", "Vec4", "Mat3", "Mat4"};
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::Combo("##type", &type_idx, types, 7)) {
                p.type = (tryengine::graphics::ShaderParamType) type_idx;
                is_asset_dirty_ = true;
            }

            ImGui::PopID();
        }

        if (ImGui::Button("Add Parameter")) {
            current_shader_def_.params.push_back({"u_NewParam", tryengine::graphics::ShaderParamType::Float, {0.0f}});
            is_asset_dirty_ = true;
        }
        ImGui::PopID(); // ParamsList

        ImGui::Spacing();

        // ==========================================
        // Блок Текстурных слотов
        // ==========================================
        ImGui::SeparatorText("Texture Slots");
        ImGui::PushID("TexturesList");
        for (int i = 0; i < (int) current_shader_def_.textures.size(); ++i) {
            ImGui::PushID(i);
            auto& t = current_shader_def_.textures[i];

            // Кнопка удаления
            if (ImGui::Button("X")) {
                current_shader_def_.textures.erase(current_shader_def_.textures.begin() + i);
                is_asset_dirty_ = true;
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();

            // Имя текстуры (например: "u_AlbedoMap")
            char buf[64];
            strcpy(buf, t.name.c_str());
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText("##tex_name", buf, 64)) {
                t.name = buf;
                is_asset_dirty_ = true;
            }
            ImGui::SameLine();

            // Номер биндинг-слота (slot)
            ImGui::Text("Slot");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            int slot_val = (int) t.slot;
            if (ImGui::InputInt("##tex_slot", &slot_val, 1, 1)) {
                // Запрещаем уходить в минус, так как слот это uint32_t
                t.slot = (uint32_t) std::max(0, slot_val);
                is_asset_dirty_ = true;
            }

            ImGui::PopID();
        }

        if (ImGui::Button("Add Texture Slot")) {
            // Находим максимальный текущий слот, чтобы новый добавился со следующим номером
            uint32_t next_slot = 0;
            for (const auto& t : current_shader_def_.textures) {
                if (t.slot >= next_slot) {
                    next_slot = t.slot + 1;
                }
            }
            current_shader_def_.textures.push_back({"albedo_map", next_slot});
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
                archive(current_shader_def_);
            } catch (const std::exception& e) {
                std::cerr << "Shader Inspector Load Error: " << e.what() << std::endl;
            }
        }
        is_asset_dirty_ = false;
    }

    void SaveAsset(const std::filesystem::path& path) {
        // Шаг 1: Сохраняем ИСХОДНИК (.shader в JSON формате)
        {
            std::ofstream os(path);
            if (os.is_open()) {
                cereal::JSONOutputArchive archive(os);
                archive(cereal::make_nvp("shader_def", current_shader_def_));
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

                    // Если у тебя заголовок AssetMetaHeader уже подключен, используй его.
                    // Иначе вот локальная структура для быстрого десериализатора:
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

            // Убеждаемся, что папка существует (на всякий случай)
            std::filesystem::create_directories(folder);

            std::filesystem::path artefact_path = folder / (std::to_string(guid) + ".shd");

            std::ofstream bin_os(artefact_path, std::ios::binary);
            if (bin_os.is_open()) {
                cereal::BinaryOutputArchive bin_archive(bin_os);
                bin_archive(current_shader_def_);
            } else {
                std::cerr << "Failed to write artifact at: " << artefact_path << std::endl;
            }
        }

        // Сбрасываем флаг после успешного сохранения
        is_asset_dirty_ = false;
    }

    std::filesystem::path current_asset_path_;
    tryengine::graphics::ShaderAsset current_shader_def_;
    bool is_asset_dirty_ = false;
};

}  // namespace tryeditor