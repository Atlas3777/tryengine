#include "editor/import/ImportSystem.hpp"

namespace tryeditor {

IAssetImporter* ImportSystem::GetImporterByName(eastl::string_view name) const {
    auto it = importers_by_name_.find(eastl::string(name));
    if (it != importers_by_name_.end()) {
        return it->second;
    }
    return nullptr;
}

IAssetImporter* ImportSystem::GetImporterByExtension(eastl::string_view ext) const {
    auto it = importers_by_ext_.find(eastl::string(ext));
    if (it != importers_by_ext_.end()) {
        return it->second;
    }
    return nullptr;
}

}  // namespace tryeditor