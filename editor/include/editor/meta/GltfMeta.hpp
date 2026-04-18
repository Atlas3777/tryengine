#pragma once
#include <cereal/archives/json.hpp>

#include "editor/meta/AssetMetaHeader.hpp"

namespace tryeditor {

struct GltfImportSettings {
    float scale;
    // some
};

struct GltfMeta {
    AssetMetaHeader asset_header;
    GltfImportSettings import_settings;
};

template <class Archive>
void serialize(Archive& archive, AssetMetaHeader& header) {
    archive(cereal::make_nvp("uuid", header.guid));
}

template <class Archive>
void serialize(Archive& archive, GltfImportSettings& settings) {
    archive(cereal::make_nvp("scale", settings.scale));
}

template <class Archive>
void serialize(Archive& archive, GltfMeta& meta) {
    archive(cereal::make_nvp("header", meta.asset_header), cereal::make_nvp("settings", meta.import_settings));
}
}  // namespace tryeditor