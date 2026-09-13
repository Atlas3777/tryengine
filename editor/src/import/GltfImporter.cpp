#include "editor/import/GltfImporter.hpp"

#include <hlsl++/matrix_float_type.h>
#include <hlsl++/quaternion_type.h>
#include <hlsl++/quaternion.h>
#include <hlsl++.h>

#include "editor/JsonParser.hpp"
#include "editor/asset_factories/MaterialAssetFactory.hpp"
#include "editor/import/MeshProcessor.hpp"
#include "editor/import/TextureImporter.hpp"
#include "editor/import/TextureProcessor.hpp"
#include "editor/meta/ModelAssetMap.hpp"
#include "engine/core/FormatUtils.h"
#include "engine/core/Random.hpp"
#include "engine/graphics/AssetTypes.hpp"
#include "engine/resources/AssetTypes.hpp"
#include "engine/resources/Content.hpp"
#include "engine/resources/TextureBinary.hpp"
#include "engine/resources/TextureTypes.hpp"
#include "tiny_gltf_v3.h"

namespace tryeditor {
using tryengine::resources::SamplerAddressMode;
using tryengine::resources::TextureFilter;

namespace {

int FindAttribute(const tg3_primitive& prim, const char* name) {
    for (uint32_t i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].key.len > 0 &&
            strncmp(prim.attributes[i].key.data, name, prim.attributes[i].key.len) == 0) {
            return prim.attributes[i].value;
        }
    }
    return -1;
}

const uint8_t* GetAccessorData(const tg3_model* m, int accessor_index, uint32_t& out_stride, uint32_t& out_count) {
    if (accessor_index < 0)
        return nullptr;
    const tg3_accessor& acc = m->accessors[accessor_index];
    if (acc.buffer_view < 0)
        return nullptr;

    const tg3_buffer_view& bv = m->buffer_views[acc.buffer_view];
    const tg3_buffer& buf = m->buffers[bv.buffer];

    out_stride = tg3_accessor_byte_stride(&acc, &bv);
    out_count = acc.count;

    return buf.data.data + bv.byte_offset + acc.byte_offset;
}

eastl::vector<uint64_t> ProcessMaterials(ImportResult& result, const tg3_model* m, uint64_t main_uuid,
                                          ModelAssetMap& asset_map) {
    eastl::vector<uint64_t> material_guids;
    material_guids.reserve(m->materials_count);

    for (uint32_t i = 0; i < m->materials_count; ++i) {
        const tg3_material& gltf_mat = m->materials[i];

        eastl::string mat_name = gltf_mat.name.len > 0
            ? eastl::string(gltf_mat.name.data, gltf_mat.name.len)
            : tryengine::fmt::format("Material_{}", i);

        uint64_t mat_sub_id = tryengine::core::random::CombineID(main_uuid, mat_name);
        material_guids.push_back(mat_sub_id);

        tryengine::graphics::MaterialAsset mat_data;
        mat_data.name = mat_name;
        mat_data.shader_asset_id = tryengine::resources::assets::DEFAULT_PBR_SHADER;

        const auto& pbr = gltf_mat.pbr_metallic_roughness;

        // Поля MaterialUBO в Standard.slang: albedo_color, roughness, metallic
        mat_data.scalar_params["albedo_color"] = {
            static_cast<float>(pbr.base_color_factor[0]),
            static_cast<float>(pbr.base_color_factor[1]),
            static_cast<float>(pbr.base_color_factor[2]),
            static_cast<float>(pbr.base_color_factor[3])
        };

        mat_data.scalar_params["roughness"] = {static_cast<float>(pbr.roughness_factor)};
        mat_data.scalar_params["metallic"] = {static_cast<float>(pbr.metallic_factor)};

        // Обработка текстуры albedo
        if (pbr.base_color_texture.index >= 0 && pbr.base_color_texture.index < static_cast<int>(m->textures_count)) {
            const tg3_texture& gltf_tex = m->textures[pbr.base_color_texture.index];

            if (gltf_tex.source >= 0 && gltf_tex.source < static_cast<int>(m->images_count)) {
                const tg3_image& gltf_img = m->images[gltf_tex.source];

                eastl::string tex_name = gltf_img.name.len > 0
                    ? eastl::string(gltf_img.name.data, gltf_img.name.len)
                    : tryengine::fmt::format("Texture_{}", gltf_tex.source);

                tryengine::resources::Sampler sampler{}; // Дефолтные настройки (Linear/Repeat)

                if (gltf_tex.sampler >= 0 && gltf_tex.sampler < static_cast<int>(m->samplers_count)) {
                    const tg3_sampler& s = m->samplers[gltf_tex.sampler];
                    sampler.min_filter = MapGltfFilter(s.min_filter);
                    sampler.mag_filter = MapGltfFilter(s.mag_filter);
                    sampler.address_mode_u = MapGltfWrap(s.wrap_s);
                    sampler.address_mode_v = MapGltfWrap(s.wrap_t);
                }

                uint64_t expected_tex_guid = tryengine::core::random::CombineID(main_uuid, tex_name);

                mat_data.texture_params["albedo_map"] = tryengine::graphics::TextureBindingAssets(expected_tex_guid, sampler);
            }
        }

        auto serialized_mat = Serialize(mat_data);
        if (!serialized_mat.has_value()) {
            LogCritical("Failed to serialize material {}", mat_name.c_str());
            continue;
        }

        ProducedArtifact mat_artifact;
        mat_artifact.sub_guid = mat_sub_id;
        mat_artifact.target = ArtifactTarget::Runtime;
        mat_artifact.extension = ".matbin";
        mat_artifact.bytes = std::move(*serialized_mat);

        result.artifacts.push_back(std::move(mat_artifact));
        asset_map.sub_assets.push_back({mat_sub_id, tryengine::fmt::format("{}", mat_sub_id)});
    }

    return material_guids;
}

void ProcessTextures(ImportResult& result, const tg3_model* m, uint64_t main_uuid, ModelAssetMap& asset_map) {
    for (uint32_t i = 0; i < m->images_count; ++i) {
        const tg3_image& gltf_img = m->images[i];

        eastl::string base_name = gltf_img.name.len > 0
            ? eastl::string(gltf_img.name.data, gltf_img.name.len)
            : tryengine::fmt::format("Texture_{}", i);

        uint64_t tex_sub_id = tryengine::core::random::CombineID(main_uuid, base_name);

        const u_char* compressed_data = nullptr;
        size_t compressed_size = 0;

        if (gltf_img.buffer_view >= 0) {
            const tg3_buffer_view& bv = m->buffer_views[gltf_img.buffer_view];
            compressed_data = m->buffers[bv.buffer].data.data + bv.byte_offset;
            compressed_size = bv.byte_length;
        }

        if (!compressed_data || compressed_size == 0)
            continue;

        eastl::span<const uint8_t> input_bytes(compressed_data, compressed_size);

        TextureProcessSettings settings;
        settings.format = tryengine::resources::TextureFormat::TEXTUREFORMAT_BC7_RGBA_UNORM;
        auto process_result = TextureProcessor::ProcessFromEncodedMemory(input_bytes, settings);

        if (!process_result.has_value()) {
            LogError("Failed to process texture {}: {}", base_name.c_str(), process_result.error().Message());
            continue;
        }

        ProducedArtifact tex_artifact;
        tex_artifact.sub_guid = tex_sub_id;
        tex_artifact.target = ArtifactTarget::Runtime;
        tex_artifact.extension = ".tex";
        tex_artifact.bytes = std::move(*process_result);

        result.artifacts.push_back(std::move(tex_artifact));
        asset_map.sub_assets.push_back({tex_sub_id, tryengine::fmt::format("{}.tex", tex_sub_id)});
    }
}

eastl::vector<eastl::vector<uint64_t>> ProcessMeshes(ImportResult& result, const tg3_model* m, uint64_t main_uuid,
                                                       ModelAssetMap& asset_map) {
    eastl::vector<eastl::vector<uint64_t>> mesh_primitive_guids(m->meshes_count);

    for (uint32_t i = 0; i < m->meshes_count; ++i) {
        const tg3_mesh& gltf_mesh = m->meshes[i];
        eastl::string mesh_name = gltf_mesh.name.len > 0 ? eastl::string(gltf_mesh.name.data, gltf_mesh.name.len)
                                                           : tryengine::fmt::format("Mesh_{}", i);

        for (uint32_t p = 0; p < gltf_mesh.primitives_count; ++p) {
            eastl::string prim_name = tryengine::fmt::format("{}_prim_{}", mesh_name, p);
            uint64_t prim_sub_id = tryengine::core::random::CombineID(main_uuid, prim_name);
            const tg3_primitive& prim = gltf_mesh.primitives[p];

            RawPrimitiveInput raw_input;

            int pos_idx    = FindAttribute(prim, "POSITION");
            int norm_idx   = FindAttribute(prim, "NORMAL");
            int uv_idx     = FindAttribute(prim, "TEXCOORD_0");
            int color_idx  = FindAttribute(prim, "COLOR_0");
            int joint_idx  = FindAttribute(prim, "JOINTS_0");
            int weight_idx = FindAttribute(prim, "WEIGHTS_0");

            raw_input.positions.data = GetAccessorData(m, pos_idx, raw_input.positions.stride, raw_input.positions.count);
            raw_input.normals.data   = GetAccessorData(m, norm_idx, raw_input.normals.stride, raw_input.normals.count);
            raw_input.uvs.data       = GetAccessorData(m, uv_idx, raw_input.uvs.stride, raw_input.uvs.count);
            raw_input.colors.data    = GetAccessorData(m, color_idx, raw_input.colors.stride, raw_input.colors.count);
            raw_input.joints.data    = GetAccessorData(m, joint_idx, raw_input.joints.stride, raw_input.joints.count);
            raw_input.weights.data   = GetAccessorData(m, weight_idx, raw_input.weights.stride, raw_input.weights.count);

            if (color_idx >= 0) {
                const tg3_accessor& color_acc = m->accessors[color_idx];
                raw_input.color_components = (color_acc.type == TG3_TYPE_VEC3) ? 3 : 4;
            }

            if (joint_idx >= 0) {
                const tg3_accessor& joint_acc = m->accessors[joint_idx];
                raw_input.joint_component_type = joint_acc.component_type;
            }

            if (weight_idx >= 0) {
                const tg3_accessor& weight_acc = m->accessors[weight_idx];
                raw_input.weight_component_type = weight_acc.component_type;
            }

            if (prim.indices >= 0) {
                raw_input.indices.data = GetAccessorData(m, prim.indices, raw_input.indices.stride, raw_input.indices.count);
                const tg3_accessor& idx_acc = m->accessors[prim.indices];
                if (idx_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT) raw_input.indices.format = tryengine::resources::IndexFormat::UInt16;
                else if (idx_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT) raw_input.indices.format = tryengine::resources::IndexFormat::UInt32;
                else if (idx_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    TRY_ASSERT(false, "Unsupported index buffer format");
                    raw_input.indices.format = tryengine::resources::IndexFormat::UInt8;
                }
            }

            // --- Запекание меша ---
            MeshProcessSettings process_settings;
            process_settings.auto_select_format = true; // Автоматически выберет SkinnedPacked / StaticPacked / PositionOnly

            auto process_result = MeshProcessor::ProcessPrimitive(raw_input, process_settings);
            if (!process_result.has_value()) {
                LogError("Failed to process mesh primitive {}: {}", prim_name.c_str(), process_result.error().Message());
                continue;
            }

            mesh_primitive_guids[i].push_back(prim_sub_id);

            ProducedArtifact mesh_artifact;
            mesh_artifact.sub_guid = prim_sub_id;
            mesh_artifact.target = ArtifactTarget::Runtime;
            mesh_artifact.extension = ".mesh";
            mesh_artifact.bytes = std::move(*process_result);

            result.artifacts.push_back(std::move(mesh_artifact));
            asset_map.sub_assets.push_back({prim_sub_id, tryengine::fmt::format("{}.mesh", prim_sub_id)});
        }
    }

    return mesh_primitive_guids;
}

void ProcessNodes(const tg3_model* m, ModelAssetMap& asset_map,
                   const eastl::vector<eastl::vector<uint64_t>>& mesh_primitive_guids,
                   const eastl::vector<uint64_t>& material_guids) {
    asset_map.nodes.resize(m->nodes_count);

    for (uint32_t i = 0; i < m->nodes_count; ++i) {
        const tg3_node& gltf_node = m->nodes[i];

        ModelNodeData node_data;
        node_data.name = gltf_node.name.len > 0 ? eastl::string(gltf_node.name.data, gltf_node.name.len)
                                                 : tryengine::fmt::format("Node_{}", i);

        // --- ТРАНСФОРМ ---
        if (gltf_node.has_matrix) {
            // В glTF матрица хранится по столбцам (column-major)
            hlslpp::float3 col0(static_cast<float>(gltf_node.matrix[0]), static_cast<float>(gltf_node.matrix[1]), static_cast<float>(gltf_node.matrix[2]));
            hlslpp::float3 col1(static_cast<float>(gltf_node.matrix[4]), static_cast<float>(gltf_node.matrix[5]), static_cast<float>(gltf_node.matrix[6]));
            hlslpp::float3 col2(static_cast<float>(gltf_node.matrix[8]), static_cast<float>(gltf_node.matrix[9]), static_cast<float>(gltf_node.matrix[10]));

            // Позиция находится в 4-м столбце (индексы 12, 13, 14)
            node_data.local_transform.position = hlslpp::float3(static_cast<float>(gltf_node.matrix[12]),
                                                                static_cast<float>(gltf_node.matrix[13]),
                                                                static_cast<float>(gltf_node.matrix[14]));

            // Масштаб — это длины базисных векторов
            node_data.local_transform.scale = hlslpp::float3(hlslpp::length(col0), hlslpp::length(col1), hlslpp::length(col2));

            // Нормализуем столбцы для получения чистой матрицы вращения 3x3
            if (node_data.local_transform.scale.x > 0.0f) col0 /= node_data.local_transform.scale.x;
            if (node_data.local_transform.scale.y > 0.0f) col1 /= node_data.local_transform.scale.y;
            if (node_data.local_transform.scale.z > 0.0f) col2 /= node_data.local_transform.scale.z;

            // Конструируем float3x3 построчно (row0, row1, row2)
            hlslpp::float3x3 rot_mat(
                col0.x, col1.x, col2.x,
                col0.y, col1.y, col2.y,
                col0.z, col1.z, col2.z
            );

            node_data.local_transform.rotation = hlslpp::quaternion(rot_mat);
        } else {
            node_data.local_transform.position = hlslpp::float3(static_cast<float>(gltf_node.translation[0]),
                                                                static_cast<float>(gltf_node.translation[1]),
                                                                static_cast<float>(gltf_node.translation[2]));

            // glTF хранит кватернион как [x, y, z, w], что идеально совпадает с hlslpp::quaternion(x, y, z, w)
            node_data.local_transform.rotation = hlslpp::quaternion(
                static_cast<float>(gltf_node.rotation[0]), static_cast<float>(gltf_node.rotation[1]),
                static_cast<float>(gltf_node.rotation[2]), static_cast<float>(gltf_node.rotation[3]));

            node_data.local_transform.scale = hlslpp::float3(static_cast<float>(gltf_node.scale[0]),
                                                               static_cast<float>(gltf_node.scale[1]),
                                                               static_cast<float>(gltf_node.scale[2]));
        }
        for (uint32_t c = 0; c < gltf_node.children_count; ++c) {
            node_data.children_indices.push_back(gltf_node.children[c]);
        }

        if (gltf_node.mesh >= 0 && gltf_node.mesh < static_cast<int32_t>(m->meshes_count)) {
            const tg3_mesh& gltf_mesh = m->meshes[gltf_node.mesh];
            const auto& prim_guids = mesh_primitive_guids[gltf_node.mesh];

            if (gltf_mesh.primitives_count == 1) {
                const tg3_primitive& prim = gltf_mesh.primitives[0];

                node_data.mesh_id = prim_guids.empty() ? 0 : prim_guids[0];
                node_data.material_id = (prim.material >= 0 && prim.material < (int32_t)material_guids.size())
                                            ? material_guids[prim.material]
                                            : 0;  // 0 == движковый материал по умолчанию

            } else if (gltf_mesh.primitives_count > 1) {
                for (uint32_t p = 0; p < gltf_mesh.primitives_count; ++p) {
                    const tg3_primitive& prim = gltf_mesh.primitives[p];

                    ModelNodeData virtual_child;
                    virtual_child.name = tryengine::fmt::format("{}_prim_{}", node_data.name, p);
                    virtual_child.mesh_id = (p < prim_guids.size()) ? prim_guids[p] : 0;
                    virtual_child.material_id =
                        (prim.material >= 0 && prim.material < (int32_t)material_guids.size())
                            ? material_guids[prim.material]
                            : 0;
                    // Виртуальная нода наследует локальный трансформ ноды-владельца через
                    // идентичность (нулевой трансформ), т.к. её родитель уже несёт трансформ модели.

                    const auto virtual_index = static_cast<int32_t>(asset_map.nodes.size());
                    asset_map.nodes.push_back(std::move(virtual_child));  // safe: no live reference into the vector right now
                    node_data.children_indices.push_back(virtual_index);
                }
            }
        }

        asset_map.nodes[i] = std::move(node_data);  // write back once, after all growth for this node
    }

    const int32_t scene_idx = m->default_scene >= 0 ? m->default_scene : 0;
    if (scene_idx < (int32_t)m->scenes_count) {
        const tg3_scene& scene = m->scenes[scene_idx];
        for (uint32_t i = 0; i < scene.nodes_count; ++i) {
            asset_map.scene_roots.push_back(scene.nodes[i]);
        }
    }
}

}  // namespace

AssetMeta<GltfImportSettings> GltfImporter::GenerateDefaultMeta() const {
    AssetMeta<GltfImportSettings> result;
    result.header.guid = tryengine::core::random::GenerateInt64();
    result.header.importer_type = GetName();
    result.settings = GltfImportSettings{};

    return result;
}

[[nodiscard]] tryengine::async::Task<ImportResult> GltfImporter::Import(ImportContext ctx) const {
    LogCritical("Starting import GLTF");
    ImportResult result;

    AssetMeta<GltfImportSettings> meta;
    if (!ctx.meta_bytes.empty()) {
        auto parsed_meta = Deserialize<AssetMeta<GltfImportSettings>>(ctx.meta_bytes);
        meta = parsed_meta.has_value() ? parsed_meta.value() : GenerateDefaultMeta();
    } else {
        meta = GenerateDefaultMeta();
    }
    result.main_guid = meta.header.guid;

    ModelAssetMap asset_map;
    tinygltf3::Model model;
    tinygltf3::ErrorStack errors;

    tg3_error_code err = tinygltf3::parse(model, errors, ctx.source_bytes.data(), ctx.source_bytes.size());

    if (err != TG3_OK) {
        co_return tryengine::core::Error("Failed to parse gltf model");
    }
    const tg3_model* m = model.get();

    eastl::vector<uint64_t> material_guids = ProcessMaterials(result, m, meta.header.guid, asset_map);
    ProcessTextures(result, m, meta.header.guid, asset_map);
    eastl::vector<eastl::vector<uint64_t>> mesh_primitive_guids = ProcessMeshes(result, m, meta.header.guid, asset_map);
    ProcessNodes(m, asset_map, mesh_primitive_guids, material_guids);

    ProducedArtifact map_artifact;
    auto asset_map_bytes = Serialize(asset_map);
    if (!asset_map_bytes.has_value()) {
        co_return tryengine::core::Error("Failed to serialize ModelAssetMap");
    }
    map_artifact.bytes = *asset_map_bytes;
    map_artifact.sub_guid = static_cast<uint32_t>(tryengine::EditorArtifactGuid::AssetMap);
    map_artifact.target = ArtifactTarget::Editor;

    result.artifacts.push_back(std::move(map_artifact));

    for (const auto& [key, _] : asset_map.sub_assets) {
        meta.header.sub_assets.push_back(key);
    }

    auto meta_bytes = Serialize(meta);
    if (meta_bytes.has_value()) {
        result.meta_bytes = *meta_bytes;
    } else {
        co_return tryengine::core::Error("Failed to serialize AssetMeta");
    }

    LogCritical("END IMPORTING GLTF");

    co_return result;
}

}  // namespace tryeditor