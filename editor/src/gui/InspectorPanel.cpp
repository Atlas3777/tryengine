#include "editor/gui/InspectorPanel.hpp"

#include <cereal/archives/json.hpp>
#include <entt/entt.hpp>
#include <entt/meta/meta.hpp>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>

#include "editor/Components.hpp"
#include "editor/EditorGUIUtils.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"

namespace tryeditor {

void InspectorPanel::OnImGuiRender(entt::registry& reg) {
    ImGui::Begin("Inspector");

    auto selected_entity = reg.view<SelectedTag>().front();

    if (selected_entity != entt::null) {
        DrawEntityInspector(reg);
    } else if (!editor_context_.selected_asset_path.empty()) {
        DrawAssetInspector(editor_context_.selected_asset_path);
    } else {
        ImGui::TextDisabled("Select an entity or asset to inspect.");
    }
    ImGui::End();
}

void InspectorPanel::DrawAssetInspector(const std::filesystem::path& path) const {
    if (std::filesystem::is_directory(path)) {
        ImGui::TextDisabled("Selected: Directory");
        ImGui::Text("%s", path.string().c_str());
        return;
    }

    ImGui::Text("Asset: %s", path.filename().string().c_str());
    ImGui::Separator();

    // 1. Читаем заголовок метафайла, чтобы понять, какой инспектор вызвать
    std::filesystem::path meta_path = path.string() + ".meta";

    if (const auto header_opt = import_system_.PeekMetaHeader(meta_path)) {
        // 2. Делегируем отрисовку менеджеру по ТИПУ АССЕТА
        asset_inspector_manager_.Draw(path, header_opt->asset_type);
    } else {
        ImGui::TextDisabled("No valid .meta file found for this asset.");
    }
}

// void InspectorPanel::DrawShaderSlots() {
//     // Отрисовка слота для Vertex Shader
//     ImGui::Text("Vertex Shader:");
//     ImGui::SameLine();
//
//     // Создаем кнопку, которая выглядит как слот. Если ID = 0, пишем "None"
//     std::string vert_label = current_shader_def_.vertex_shader_id == 0
//                                  ? "[ Drop .vert here ]"
//                                  : "ID: " + std::to_string(current_shader_def_.vertex_shader_id);
//
//     ImGui::Button(vert_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0));
//
//     // DRAG AND DROP ПРИЕМНИК (TARGET)
//     if (ImGui::BeginDragDropTarget()) {
//         // Принимаем payload с типом "CONTENT_BROWSER_ID" (тот самый, что ты задал в FileBrowserPanel)
//         if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ID")) {
//             uint64_t dropped_id = *(const uint64_t*) payload->Data;
//
//             // Здесь в идеале нужно проверить через AssetDatabase/ImportSystem,
//             // что dropped_id действительно принадлежит файлу с расширением .vert
//
//             current_shader_def_.vertex_shader_id = dropped_id;
//             is_asset_dirty_ = true;  // Помечаем, что нужно перезаписать JSON
//         }
//         ImGui::EndDragDropTarget();
//     }
//
//     // Отрисовка слота для Fragment Shader (Абсолютно аналогично)
//     ImGui::Text("Fragment Shader:");
//     ImGui::SameLine();
//     std::string frag_label = current_shader_def_.fragment_shader_id == 0
//                                  ? "[ Drop .frag here ]"
//                                  : "ID: " + std::to_string(current_shader_def_.fragment_shader_id);
//
//     ImGui::Button(frag_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0));
//
//     if (ImGui::BeginDragDropTarget()) {
//         if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ID")) {
//             uint64_t dropped_id = *(const uint64_t*) payload->Data;
//             current_shader_def_.fragment_shader_id = dropped_id;
//             is_asset_dirty_ = true;
//         }
//         ImGui::EndDragDropTarget();
//     }
// }

void InspectorPanel::DrawEntityInspector(entt::registry& reg) {
    ImGui::Begin("Inspector");

    auto view = reg.view<SelectedTag>();
    if (view.empty()) {
        ImGui::TextDisabled("Select an entity to inspect.");
    } else {
        auto entity = view.front();

        ImGui::Text("Entity ID: %u", static_cast<uint32_t>(entity));

        if (auto* tag = reg.try_get<tryengine::Tag>(entity)) {
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, tag->tag.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Tag", buffer, sizeof(buffer))) {
                tag->tag = std::string(buffer);
            }
        }

        ImGui::Separator();

        for (auto [id, storage] : reg.storage()) {
            if (storage.contains(entity)) {
                if (id == entt::type_hash<SelectedTag>::value())
                    continue;
                if (id == entt::type_hash<tryengine::Tag>::value())
                    continue;

                auto type = entt::resolve(id);
                if (type) {
                    DrawMetaComponent(reg, entity, type);
                } else {
                    DrawUnregisteredComponent(reg, entity, id);
                }
            }
        }

        ImGui::Separator();
        DrawAddComponentButton(reg, entity);
    }

    ImGui::End();
}

void InspectorPanel::DrawMetaField(entt::meta_any& instance, entt::meta_data data) {
    const char* name = data.name();
    if (!name)
        return;

    entt::meta_any value = data.get(instance);
    if (!value)
        return;

    ImGui::PushID(name);
    bool changed = false;

    entt::meta_any custom_any = data.custom();

    // Проверяем, содержит ли custom_any наш AssetTypeID
    if (custom_any) {
        if (auto* expected_type_ptr = custom_any.try_cast<tryengine::AssetTypeID>()) {
            if (auto* val = value.try_cast<uint64_t>()) {
                tryengine::AssetTypeID expected_type = *expected_type_ptr;

                if (ImGuiExt::AssetSlot(name, *val, expected_type)) {
                    changed = true;
                }
            }
        }
    }

    if (auto* val = value.try_cast<glm::vec3>()) {
        if (ImGui::DragFloat3(name, glm::value_ptr(*val), 0.1f))
            changed = true;
    } else if (auto* val = value.try_cast<float>()) {
        if (ImGui::DragFloat(name, val, 0.1f))
            changed = true;
    } else if (auto* val = value.try_cast<int>()) {
        if (ImGui::DragInt(name, val))
            changed = true;
    } else if (auto* val = value.try_cast<uint64_t>()) {
        // ImGuiDataType_U64 позволяет работать с 64-битными беззнаковыми числами
        if (ImGui::InputScalar(name, ImGuiDataType_U64, val)) {
            changed = true;
        }
    } else if (auto* val = value.try_cast<entt::entity>()) {
        // Отображаем ID сущности (только чтение, так как менять ID вручную опасно)
        uint64_t entityId = static_cast<uint64_t>(*val);
        if (*val == entt::null) {
            ImGui::Text("%s: None", name);
        } else {
            ImGui::Text("%s: Entity [%d]", name, entityId);
        }
        // Если нужно менять связь через Drag-and-Drop или поиск, логика будет здесь
    }

    else if (auto* val = value.try_cast<bool>()) {
        if (ImGui::Checkbox(name, val))
            changed = true;
    } else if (auto* val = value.try_cast<glm::quat>()) {
        glm::vec3 euler = glm::degrees(glm::eulerAngles(*val));
        if (ImGui::DragFloat3(name, glm::value_ptr(euler), 1.0f)) {
            *val = glm::quat(glm::radians(euler));
            changed = true;
        }
    } else if (auto* val = value.try_cast<std::string>()) {
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, val->c_str(), sizeof(buffer) - 1);
        if (ImGui::InputText(name, buffer, sizeof(buffer))) {
            *val = std::string(buffer);
            changed = true;
        }
    }
    // Если значение изменено, обновляем оригинальный компонент
    if (changed) {
        data.set(instance, value);
    }

    ImGui::PopID();
}
void InspectorPanel::DrawMetaComponent(entt::registry& reg, entt::entity entity, entt::meta_type type) {
    auto id = type.id();
    const char* typeName = type.name() ? type.name() : "Unknown Component";

    ImGui::PushID(static_cast<int>(id));

    // Настройка стиля заголовка компонента
    const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                             ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                                             ImGuiTreeNodeFlags_FramePadding;

    float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    bool open = ImGui::TreeNodeEx((void*) (uintptr_t) id, treeNodeFlags, "%s", typeName);

    // Кнопка настроек/удаления компонента
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - lineHeight * 0.5f);
    if (ImGui::Button("+", ImVec2{lineHeight, lineHeight})) {
        ImGui::OpenPopup("ComponentSettings");
    }

    bool removeComponent = false;
    if (ImGui::BeginPopup("ComponentSettings")) {
        if (ImGui::MenuItem("Remove Component")) {
            removeComponent = true;
        }
        ImGui::EndPopup();
    }

    if (open) {
        // Получаем указатель на компонент в памяти реестра
        void* dataPtr = reg.storage(id)->value(entity);

        // type.from_void создает meta_any, который выступает алиасом (ссылкой) на dataPtr
        entt::meta_any instance = type.from_void(dataPtr);

        if (instance) {
            for (auto [field_id, data_member] : type.data()) {
                DrawMetaField(instance, data_member);  // Передаем как &
            }
        }
        ImGui::TreePop();
    }

    if (removeComponent) {
        reg.storage(id)->remove(entity);
    }

    ImGui::PopID();
}

void InspectorPanel::DrawUnregisteredComponent(entt::registry& reg, entt::entity entity, entt::id_type id) {
    ImGui::PushID(static_cast<int>(id));

    // Используем серый цвет, чтобы визуально выделить отсутствие мета-данных
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

    // TreeNodeEx без возможности раскрытия (Leaf), так как полей мы не знаем
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
    ImGui::TreeNodeEx((void*) (uintptr_t) id, flags, "Unregistered Component [Hash: %u]", id);

    ImGui::PopStyleColor();

    // Контекстное меню для удаления даже незарегистрированных компонентов
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remove Component")) {
            reg.storage(id)->remove(entity);
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
}
void InspectorPanel::DrawAddComponentButton(entt::registry& reg, entt::entity entity) {
    ImGui::Spacing();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((width - 150.0f) * 0.5f);

    if (ImGui::Button("Add Component", ImVec2{150, 25})) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        // Перебираем все зарегистрированные в системе типы
        for (auto [id, type] : entt::resolve()) {
            auto* storage = reg.storage(id);

            // Показываем только те, которых еще нет у сущности
            if (storage && !storage->contains(entity)) {
                const char* name = type.name() ? type.name() : "Unknown";
                if (ImGui::Selectable(name)) {
                    // Используем push для добавления в хранилище
                    // ВАЖНО: для сложных типов убедитесь, что в мета-данных
                    // зарегистрирован конструктор по умолчанию, либо используйте
                    // специальные функции-хелперы для emplace.
                    storage->push(entity);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }
}
}  // namespace tryeditor
