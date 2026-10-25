#include "editor/UiTreeBuilder.hpp"

namespace tryeditor {

eastl::vector<UiFolder> UiTreeBuilder::BuildUiTree(
    const eastl::vector<RawFolder>& raw_folders,
    const eastl::hash_map<eastl::string, uint64_t>& path_to_guid) {

    eastl::vector<UiFolder> ui_folders;
    ui_folders.reserve(raw_folders.size());

    for (const auto& raw_folder : raw_folders) {
        UiFolder ui_folder;
        ui_folder.name = raw_folder.name;
        ui_folder.subfolders = raw_folder.subfolders;

        ui_folder.files.reserve(raw_folder.files.size());
        for (const auto& raw_file : raw_folder.files) {
            uint64_t guid = 0;
            auto it = path_to_guid.find(raw_file.path);
            if (it != path_to_guid.end()) {
                guid = it->second;
            }

            ui_folder.files.push_back(UiFile{
                .name = raw_file.name,
                .path = raw_file.path,
                .guid = guid
            });
        }

        ui_folders.push_back(eastl::move(ui_folder));
    }

    return ui_folders;
}

}  // namespace tryeditor