#include "editor/gui/HierarchyPanel.hpp"

#include "editor/Components.hpp"
#include "editor/SelectionManager.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"
#include "entt/entt.hpp"
#include "imgui_internal.h"

namespace tryeditor {

void HierarchyPanel::OnImGuiRender(entt::registry& reg) {
    ImGui::Begin("Hierarchy");

    HandleShortcuts(reg);
    bool isMouseCaptured = SDL_GetWindowRelativeMouseMode(SDL_GL_GetCurrentWindow());

    if (!isMouseCaptured && ImGui::BeginPopupContextWindow("HierarchyContextMenu",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty Entity")) {
            auto newEntity = reg.create();
            reg.emplace<tryengine::Transform>(newEntity);
            reg.emplace<tryengine::Relationship>(newEntity);
            reg.emplace<tryengine::Tag>(newEntity, "New Entity");

            reg.clear<SelectedTag>();
            reg.emplace<SelectedTag>(newEntity);
        }
        ImGui::EndPopup();
                                       }

    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(), ImGui::GetID("Hierarchy"))) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
            entt::entity droppedEntity = *(entt::entity*)payload->Data;
            ReparentEntity(droppedEntity, entt::null, reg);
        }
        ImGui::EndDragDropTarget();
    }

    // Отрисовка корневых сущностей
    for (auto [entity] : reg.storage<entt::entity>().each()) {
        bool isRoot = true;
        if (auto* rel = reg.try_get<tryengine::Relationship>(entity)) {
            if (rel->parent != entt::null) {
                isRoot = false;
            }
        }
        if (isRoot) {
            DrawEntityNode(entity, reg);
        }
    }

    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
        reg.clear<SelectedTag>();
    }

    if (!entities_to_destroy_.empty()) {
        for (auto e : entities_to_destroy_) {
            if (reg.valid(e)) {
                DestroyEntityRecursive(e, reg);
            }
        }
        entities_to_destroy_.clear();
    }

    ImGui::End();
}

void HierarchyPanel::DrawEntityNode(entt::entity entity, entt::registry& reg) {
    bool isSelected = reg.all_of<SelectedTag>(entity);
    ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0);
    flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    auto* rel = reg.try_get<tryengine::Relationship>(entity);
    bool hasChildren = rel && (rel->children > 0);

    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    std::string label = "Entity [" + std::to_string(static_cast<uint32_t>(entity)) + "]";
    if (auto* tag = reg.try_get<tryengine::Tag>(entity)) {
        label = tag->tag;
    }

    if (entity_to_rename_ == entity) {
        flags &= ~ImGuiTreeNodeFlags_SpanAvailWidth;
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", rename_buffer_, sizeof(rename_buffer_),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            if (!reg.all_of<tryengine::Tag>(entity)) reg.emplace<tryengine::Tag>(entity);
            reg.get<tryengine::Tag>(entity).tag = rename_buffer_;
            entity_to_rename_ = entt::null;
        }
        if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            entity_to_rename_ = entt::null;
        }
    } else {
        bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)entity, flags, "%s", label.c_str());

        if (ImGui::IsItemClicked()) {
            bool additive = ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl;
            if (additive) {
                //TODO: Реализовать множественную обработку
                if (isSelected) reg.remove<SelectedTag>(entity);
                else reg.emplace<SelectedTag>(entity);
            } else {
                selection_manager_.Select(entity, reg);
                // reg.clear<SelectedTag>();
                // reg.emplace<SelectedTag>(entity);
            }
        }

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &entity, sizeof(entt::entity));
            ImGui::Text("Move %s", label.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                entt::entity droppedEntity = *(entt::entity*)payload->Data;
                ReparentEntity(droppedEntity, entity, reg);
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Destroy Entity")) {
                entities_to_destroy_.push_back(entity);
            }
            ImGui::EndPopup();
        }

        // РЕКУРСИВНАЯ ОТРИСОВКА ЧЕРЕЗ СВЯЗНЫЙ СПИСОК (Гораздо быстрее)
        if (opened) {
            if (hasChildren) {
                entt::entity curr = rel->first;
                while (curr != entt::null) {
                    DrawEntityNode(curr, reg);
                    curr = reg.get<tryengine::Relationship>(curr).next;
                }
            }
            ImGui::TreePop();
        }
    }
}

void HierarchyPanel::HandleShortcuts(entt::registry& reg) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsAnyItemActive()) return;

    ImGuiIO& io = ImGui::GetIO();
    auto selectedView = reg.view<SelectedTag>();

    // HJKL Навигация
    if (ImGui::IsKeyPressed(ImGuiKey_J)) io.AddKeyEvent(ImGuiKey_DownArrow, true);
    if (ImGui::IsKeyReleased(ImGuiKey_J)) io.AddKeyEvent(ImGuiKey_DownArrow, false);
    if (ImGui::IsKeyPressed(ImGuiKey_K)) io.AddKeyEvent(ImGuiKey_UpArrow, true);
    if (ImGui::IsKeyReleased(ImGuiKey_K)) io.AddKeyEvent(ImGuiKey_UpArrow, false);
    if (ImGui::IsKeyPressed(ImGuiKey_L)) io.AddKeyEvent(ImGuiKey_RightArrow, true);
    if (ImGui::IsKeyReleased(ImGuiKey_L)) io.AddKeyEvent(ImGuiKey_RightArrow, false);
    if (ImGui::IsKeyPressed(ImGuiKey_H)) io.AddKeyEvent(ImGuiKey_LeftArrow, true);
    if (ImGui::IsKeyReleased(ImGuiKey_H)) io.AddKeyEvent(ImGuiKey_LeftArrow, false);

    if (!selectedView.empty()) {
        // УДАЛЕНИЕ (D)
        // if (ImGui::IsKeyPressed(ImGuiKey_D)) {
        //     m_EntitiesToDestroy.insert(m_EntitiesToDestroy.end(), selectedView.begin(), selectedView.end());
        // }

        // // ПЕРЕИМЕНОВАНИЕ (R)
        // if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        //     m_EntityToRename = selectedView.front();
        //     std::string currentName =
        //         reg.all_of<Tag>(m_EntityToRename) ? reg.get<Tag>(m_EntityToRename).tag : "";
        //     strncpy(m_RenameBuffer, currentName.c_str(), sizeof(m_RenameBuffer));
        // }

        // ВЫРЕЗАТЬ (X)
        if (ImGui::IsKeyPressed(ImGuiKey_X)) {
            clipboard_entities_.assign(selectedView.begin(), selectedView.end());
            is_cut_operation_ = true;
        }

        // КОПИРОВАТЬ (Y)
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
            clipboard_entities_.assign(selectedView.begin(), selectedView.end());
            is_cut_operation_ = false;
        }
    }

    // ВСТАВКА (P)
    if (ImGui::IsKeyPressed(ImGuiKey_P) && !clipboard_entities_.empty()) {
        entt::entity targetParent = selectedView.empty() ? entt::null : selectedView.front();

        for (entt::entity clipboardEntity : clipboard_entities_) {
            if (!reg.valid(clipboardEntity)) continue;

            if (is_cut_operation_) {
                ReparentEntity(clipboardEntity, targetParent, reg);
            } else {
                entt::entity newEntity = CloneEntity(clipboardEntity, reg);
                ReparentEntity(newEntity, targetParent, reg);
            }
        }

        if (is_cut_operation_) {
            clipboard_entities_.clear();
        }
    }
}

void HierarchyPanel::ReparentEntity(entt::entity entity, entt::entity newParent, entt::registry& reg) {
    if (entity == newParent) return;

    auto& rel = reg.get_or_emplace<tryengine::Relationship>(entity);

    // --- защита от циклов ---
    entt::entity check = newParent;
    while (check != entt::null) {
        if (check == entity) return;
        auto* parentRel = reg.try_get<tryengine::Relationship>(check);
        if (!parentRel) break;
        check = parentRel->parent;
    }

    // 1. Отвязываем от старого родителя
    if (rel.parent != entt::null) {
        auto& oldParentRel = reg.get<tryengine::Relationship>(rel.parent);
        oldParentRel.children--;

        if (rel.prev != entt::null) {
            reg.get<tryengine::Relationship>(rel.prev).next = rel.next;
        } else {
            // Если предыдущего нет, значит это был первый ребенок
            oldParentRel.first = rel.next;
        }

        if (rel.next != entt::null) {
            reg.get<tryengine::Relationship>(rel.next).prev = rel.prev;
        }
    }

    // 2. Сбрасываем старые связи
    rel.parent = newParent;
    rel.prev = entt::null;
    rel.next = entt::null;

    // 3. Привязываем к новому родителю (вставляем в начало списка для скорости)
    if (newParent != entt::null) {
        auto& newParentRel = reg.get_or_emplace<tryengine::Relationship>(newParent);
        newParentRel.children++;

        if (newParentRel.first != entt::null) {
            reg.get<tryengine::Relationship>(newParentRel.first).prev = entity;
            rel.next = newParentRel.first;
        }
        newParentRel.first = entity;
    }
}

entt::entity HierarchyPanel::CloneEntity(entt::entity source, entt::registry& reg) {
    entt::entity newEntity = reg.create();

    for (auto [id, storage] : reg.storage()) {
        if (storage.contains(source)) {
            storage.push(newEntity, storage.value(source));
        }
    }

    return newEntity;
}
void HierarchyPanel::DestroyEntityRecursive(entt::entity entity, entt::registry& reg) {
    // Сначала отвязываем корень удаляемой ветки от дерева, чтобы избежать битых ссылок у родителя
    ReparentEntity(entity, entt::null, reg);

    // Внутренняя рекурсивная лямбда для полного уничтожения ветви
    auto destroy_recursive = [&](auto& self, entt::entity e) -> void {
        if (auto* rel = reg.try_get<tryengine::Relationship>(e)) {
            entt::entity curr = rel->first;
            while (curr != entt::null) {
                entt::entity next = reg.get<tryengine::Relationship>(curr).next; // Сохраняем next до удаления
                self(self, curr);
                curr = next;
            }
        }
        reg.destroy(e);
    };

    destroy_recursive(destroy_recursive, entity);
}
}  // namespace tryeditor
