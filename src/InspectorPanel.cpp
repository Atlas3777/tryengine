#include "InspectorPanel.hpp"

#include <entt/entt.hpp>
#include <entt/meta/meta.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

#include "EngineTypes.hpp"

void InspectorPanel::OnImGuiRender(entt::registry& reg) {
    ImGui::Begin("Inspector");

    auto view = reg.view<SelectedTag>();
    if (view.empty()) {
        ImGui::TextDisabled("Select an entity to inspect.");
    } else {
        auto entity = view.front();

        ImGui::Text("Entity ID: %u", static_cast<uint32_t>(entity));

        if (auto* tag = reg.try_get<TagComponent>(entity)) {
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
                if (id == entt::type_hash<SelectedTag>::value()) continue;
                if (id == entt::type_hash<TagComponent>::value()) continue;

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
    if (!name) return;

    entt::meta_any value = data.get(instance);
    if (!value) return;

    ImGui::PushID(name);
    bool changed = false;

    if (auto* val = value.try_cast<glm::vec3>()) {
        if (ImGui::DragFloat3(name, glm::value_ptr(*val), 0.1f)) changed = true;
    } else if (auto* val = value.try_cast<float>()) {
        if (ImGui::DragFloat(name, val, 0.1f)) changed = true;
    } else if (auto* val = value.try_cast<int>()) {
        if (ImGui::DragInt(name, val)) changed = true;
    } else if (auto* val = value.try_cast<bool>()) {
        if (ImGui::Checkbox(name, val)) changed = true;
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
    bool open = ImGui::TreeNodeEx((void*)(uintptr_t)id, treeNodeFlags, "%s", typeName);

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
    ImGui::TreeNodeEx((void*)(uintptr_t)id, flags, "Unregistered Component [Hash: %u]", id);

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
