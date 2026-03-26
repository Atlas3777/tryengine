#include "editor/HierarchyPanel.hpp"

#include "editor/Components.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"
#include "entt/entt.hpp"
#include "imgui_internal.h"

namespace editor {
using namespace engine::types;
using namespace engine::core;
using namespace engine;

void HierarchyPanel::OnImGuiRender(entt::registry& reg) {
    ImGui::Begin("Hierarchy");

    // Обработка горячих клавиш
    HandleShortcuts(reg);

    // Контекстное меню по клику в пустой области
    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty Entity")) {
            auto newEntity = reg.create();
            reg.emplace<Transform>(newEntity);
            reg.emplace<Hierarchy>(newEntity);
            reg.emplace<Tag>(newEntity, "New Entity");

            reg.clear<SelectedTag>();
            reg.emplace<SelectedTag>(newEntity);
        }
        ImGui::EndPopup();
    }

    // Сброс родителя (перенос в корень) при перетаскивании в пустое место
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
        if (auto* hc = reg.try_get<Hierarchy>(entity)) {
            if (hc->parent != entt::null) {
                isRoot = false;
            }
        }
        if (isRoot) {
            DrawEntityNode(entity, reg);
        }
    }

    // Снятие выделения при клике в пустоту
    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
        reg.clear<SelectedTag>();
    }

    // Отложенное безопасное удаление сущностей
    if (!m_EntitiesToDestroy.empty()) {
        for (auto e : m_EntitiesToDestroy) {
            if (reg.valid(e)) {
                DestroyEntityRecursive(e, reg);
            }
        }
        m_EntitiesToDestroy.clear();
    }

    ImGui::End();
}

void HierarchyPanel::DrawEntityNode(entt::entity entity, entt::registry& reg) {
    bool isSelected = reg.all_of<SelectedTag>(entity);
    ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0);
    flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    bool hasChildren = false;
    auto hierarchyView = reg.view<Hierarchy>();
    for (auto child : hierarchyView) {
        if (hierarchyView.get<Hierarchy>(child).parent == entity) {
            hasChildren = true;
            break;
        }
    }

    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    std::string label = "Entity [" + std::to_string(static_cast<uint32_t>(entity)) + "]";
    if (auto* tag = reg.try_get<Tag>(entity)) {
        label = tag->tag;
    }

    if (m_EntityToRename == entity) {
        flags &= ~ImGuiTreeNodeFlags_SpanAvailWidth;
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            if (!reg.all_of<Tag>(entity)) reg.emplace<Tag>(entity);
            reg.get<Tag>(entity).tag = m_RenameBuffer;
            m_EntityToRename = entt::null;
        }
        if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_EntityToRename = entt::null;
        }
    } else {
        bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)entity, flags, "%s", label.c_str());

        // Множественное выделение (Shift / Ctrl)
        if (ImGui::IsItemClicked()) {
            bool additive = ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl;

            if (additive) {
                if (isSelected)
                    reg.remove<SelectedTag>(entity);
                else
                    reg.emplace<SelectedTag>(entity);
            } else {
                reg.clear<SelectedTag>();
                reg.emplace<SelectedTag>(entity);
            }
        }

        // --- Drag & Drop ---
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

        // Контекстное меню узла
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Destroy Entity")) {
                m_EntitiesToDestroy.push_back(entity);
            }
            ImGui::EndPopup();
        }

        // Рекурсивная отрисовка детей
        if (opened) {
            if (hasChildren) {
                for (auto child : hierarchyView) {
                    if (hierarchyView.get<Hierarchy>(child).parent == entity) {
                        DrawEntityNode(child, reg);
                    }
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
            m_ClipboardEntities.assign(selectedView.begin(), selectedView.end());
            m_IsCutOperation = true;
        }

        // КОПИРОВАТЬ (Y)
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
            m_ClipboardEntities.assign(selectedView.begin(), selectedView.end());
            m_IsCutOperation = false;
        }
    }

    // ВСТАВКА (P)
    if (ImGui::IsKeyPressed(ImGuiKey_P) && !m_ClipboardEntities.empty()) {
        entt::entity targetParent = selectedView.empty() ? entt::null : selectedView.front();

        for (entt::entity clipboardEntity : m_ClipboardEntities) {
            if (!reg.valid(clipboardEntity)) continue;

            if (m_IsCutOperation) {
                ReparentEntity(clipboardEntity, targetParent, reg);
            } else {
                entt::entity newEntity = CloneEntity(clipboardEntity, reg);
                ReparentEntity(newEntity, targetParent, reg);
            }
        }

        if (m_IsCutOperation) {
            m_ClipboardEntities.clear();
        }
    }
}

void HierarchyPanel::ReparentEntity(entt::entity entity, entt::entity newParent, entt::registry& reg) {
    if (entity == newParent) return;

    auto* hc = reg.try_get<Hierarchy>(entity);
    if (!hc) {
        hc = &reg.emplace<Hierarchy>(entity);
    }

    // --- защита от циклов ---
    entt::entity check = newParent;
    while (check != entt::null) {
        if (check == entity) return;

        auto* parentHC = reg.try_get<Hierarchy>(check);
        if (!parentHC) break;

        check = parentHC->parent;
    }

    hc->parent = newParent;
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
    auto view = reg.view<Hierarchy>();

    std::vector<entt::entity> children;

    for (auto e : view) {
        if (view.get<Hierarchy>(e).parent == entity) {
            children.push_back(e);
        }
    }

    for (auto child : children) {
        DestroyEntityRecursive(child, reg);
    }

    reg.destroy(entity);
}
}  // namespace editor
