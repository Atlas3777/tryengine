#pragma once

#include "JsonParser.hpp"
#include "engine/async/Task.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/graphics/AssetTypes.hpp"
#include "engine/resources/AsyncFileManager.hpp"
#include "meta/AssetMetaHeader.hpp"

namespace tryeditor {
struct empty{};

inline tryengine::async::Task<void> CreateFile(tryengine::resources::AsyncFileManager& file_manager) {
    tryengine::graphics::ShaderAsset asset{};
    asset.vertex_shader_id = 17044468730014679888ULL;
    asset.fragment_shader_id = 18268885610255139090ULL;

    auto data_res = Serialize(asset);
    if (!data_res.has_value())
        co_return LogAndMakeError("serialization failed");

    auto data = *data_res;

    AssetMeta<empty> meta;
    meta.header.guid = 5555;
    meta.header.importer_type = "Custom";

    auto header_opt = Serialize(meta);
    if (!header_opt.has_value())
        co_return LogAndMakeError("serialization failed");

    auto head = *header_opt;

    co_await file_manager.WriteChunkAsync("game/assets/s.shader", data);
    co_await file_manager.WriteChunkAsync("game/assets/s.shader.meta", head);
    co_return {};
}
}