#include <imgui.h>
#include "misc/cpp/imgui_stdlib.h"
#include "engine/core/Addressables.hpp"
#include "editor/AddressablesProvider.hpp"
#include <iostream>

namespace tryeditor {

class AddressablesPanel : public IPanel {
public:
    AddressablesPanel(AddressablesProvider& addressables_provider)
        : addressables_provider_(addressables_provider) {}

    const char* GetName() const override { return "AddressablesPanel"; }

    void OnImGuiRender(entt::registry& reg) override {
        if (ImGui::Begin("Addressables Groups")) {

            // Кнопка добавления новой группы
            if (ImGui::Button("Add New Group")) {
                ImGui::OpenPopup("NewGroupPopup");
            }

            // Логика всплывающего окна
            if (ImGui::BeginPopup("NewGroupPopup")) {
                static std::string new_group_name = "";
                ImGui::InputText("Group Name", &new_group_name);

                if (ImGui::Button("Create") && !new_group_name.empty()) {
                    tryengine::core::AddressablesGroupAsset new_group;
                    new_group.name = new_group_name;
                    addressables_provider_.AddGroup(new_group);
                    new_group_name.clear(); // очищаем для следующего раза
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::Spacing();

            // Настройка таблицы: 2 колонки с разделителями и возможностью изменения размера
            static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
                                           ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;

            if (ImGui::BeginTable("AddressablesTable", 2, flags)) {
                ImGui::TableSetupColumn("Asset Address / Group Name");
                ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableHeadersRow();

                auto& groups = addressables_provider_.GetAddressables().GetGroups();

                for (size_t g_idx = 0; g_idx < groups.size(); ++g_idx) {
                    auto& group = groups[g_idx];
                    ImGui::PushID(group.name.c_str());

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    bool group_open = ImGui::TreeNodeEx(group.name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth);

                    // --- КОНТЕКСТНОЕ МЕНЮ ГРУППЫ ---
                    if (ImGui::BeginPopupContextItem("GroupContext")) {
                        if (ImGui::MenuItem("Delete Group", nullptr, false)) {
                            addressables_provider_.DeleteGroup(group.name);
                            ImGui::EndPopup();
                            ImGui::PopID();
                            break; // Выходим из цикла, так как вектор изменился
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("Group (%zu)", group.map.size());

                    if (group_open) {
                        std::string key_to_rename = "";
                        std::string new_key_name = "";
                        std::string key_to_delete = "";

                        for (auto& [address, guid] : group.map) {
                            ImGui::PushID(address.c_str());
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();

                            ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());

                            // Редактирование адреса (имени внутри группы)
                            std::string temp_addr = address;
                            if (ImGui::InputText("##addr", &temp_addr, ImGuiInputTextFlags_EnterReturnsTrue)) {
                                if (!temp_addr.empty() && temp_addr != address) {
                                    key_to_rename = address;
                                    new_key_name = temp_addr;
                                }
                            }

                            // --- КОНТЕКСТНОЕ МЕНЮ АССЕТА ---
                            if (ImGui::BeginPopupContextItem("AssetContext")) {
                                if (ImGui::MenuItem("Remove from Group")) {
                                    key_to_delete = address;
                                }
                                ImGui::EndPopup();
                            }

                            ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());

                            ImGui::TableNextColumn();
                            // ТЕПЕРЬ GUID ТОЛЬКО ДЛЯ ЧТЕНИЯ
                            ImGui::Text("%llu", guid);

                            ImGui::PopID();
                        }

                        // Обработка удаления ассета
                        if (!key_to_delete.empty()) {
                            group.map.erase(key_to_delete);
                            addressables_provider_.SaveGroup(group);
                            dirty = true;
                        }

                        // Обработка переименования
                        if (!key_to_rename.empty()) {
                            uint64_t val = group.map[key_to_rename];
                            group.map.erase(key_to_rename);
                            group.map[new_key_name] = val;
                            addressables_provider_.SaveGroup(group);
                            dirty = true;
                        }

                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (dirty) {
                ImGui::Separator();
                if (ImGui::Button("Save Changes")) {
                    // Здесь вызывайте вашу логику сериализации через cereal
                    // addressables_.SaveToFile();
                    dirty = false;
                }
            }
        }
        ImGui::End();
    }

private:
    AddressablesProvider& addressables_provider_;
    bool dirty = false; // Флаг для отслеживания изменений
};

} // namespace tryeditor