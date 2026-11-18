#include "editor/import/GltfImporter.hpp"

#include <cmath>
#include <daScript/daScript.h>
#include <daScript/simulate/aot_builtin_matrix.h>

#include "editor/import/MeshProcessor.hpp"
#include "editor/import/TextureImporter.hpp"
#include "editor/import/TextureProcessor.hpp"
#include "editor/meta/ModelAssetMap.hpp"
#include "engine/core/FormatUtils.h"
#include "engine/core/Random.hpp"
#include "engine/graphics/MaterialConfig.hpp"
#include "engine/resources/AssetTypes.hpp"
#include "engine/resources/ConfigFormat.hpp"
#include "engine/resources/Content.hpp"
#include "engine/resources/Json.hpp"
#include "engine/resources/TextureTypes.hpp"
#include "engine/resources/binary/TextureBinary.hpp"
#include "tiny_gltf_v3.h"

namespace tryeditor {
namespace {

using tryengine::resources::ConfigField;
using tryengine::resources::ConfigFloat4;

ConfigField MakeFloatField(const char* name, float value) {
    ConfigField field;
    field.name = name;
    field.type_name = "f32";
    field.value = value;
    return field;
}

ConfigField MakeFloat4Field(const char* name, float x, float y, float z, float w) {
    ConfigField field;
    field.name = name;
    field.type_name = "f32x4";
    field.value = ConfigFloat4{x, y, z, w};
    return field;
}

ConfigField MakeU64Field(const char* name, uint64_t value) {
    ConfigField field;
    field.name = name;
    field.type_name = "u64";
    field.value = value;
    return field;
}

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

        eastl::string mat_name = gltf_mat.name.len > 0 ? eastl::string(gltf_mat.name.data, gltf_mat.name.len)
                                                       : tryengine::fmt::format("Material_{}", i);

        uint64_t mat_sub_id = tryengine::core::random::CombineID(main_uuid, mat_name);
        material_guids.push_back(mat_sub_id);

        // Материал — тот же ConfigFormat, что и материал из data/: один формат на всю
        // цепочку (импорт → C++-загрузчик → редактор). Поля Standard.slang:
        // albedo_color, roughness, metallic (UBO) и albedo_map (сэмплер).
        tryengine::graphics::MaterialConfig mat_data;
        mat_data.name = mat_name;
        mat_data.shader = tryengine::resources::assets::DEFAULT_PBR_SHADER;

        const auto& pbr = gltf_mat.pbr_metallic_roughness;

        mat_data.params.fields.push_back(MakeFloat4Field(
            "albedo_color", static_cast<float>(pbr.base_color_factor[0]), static_cast<float>(pbr.base_color_factor[1]),
            static_cast<float>(pbr.base_color_factor[2]), static_cast<float>(pbr.base_color_factor[3])));
        mat_data.params.fields.push_back(MakeFloatField("roughness", static_cast<float>(pbr.roughness_factor)));
        mat_data.params.fields.push_back(MakeFloatField("metallic", static_cast<float>(pbr.metallic_factor)));

        // Обработка текстуры albedo
        if (pbr.base_color_texture.index >= 0 && pbr.base_color_texture.index < static_cast<int>(m->textures_count)) {
            const tg3_texture& gltf_tex = m->textures[pbr.base_color_texture.index];

            if (gltf_tex.source >= 0 && gltf_tex.source < static_cast<int>(m->images_count)) {
                const tg3_image& gltf_img = m->images[gltf_tex.source];

                eastl::string tex_name = gltf_img.name.len > 0 ? eastl::string(gltf_img.name.data, gltf_img.name.len)
                                                               : tryengine::fmt::format("Texture_{}", gltf_tex.source);

                uint64_t expected_tex_guid = tryengine::core::random::CombineID(main_uuid, tex_name);

                mat_data.textures.fields.push_back(MakeU64Field("albedo_map", expected_tex_guid));
            }
        }

        const eastl::string serialized_mat = tryengine::graphics::WriteMaterialConfig(mat_data);

        ProducedArtifact mat_artifact;
        mat_artifact.sub_guid = mat_sub_id;
        mat_artifact.target = ArtifactTarget::Runtime;
        mat_artifact.extension = ".mat";
        mat_artifact.bytes.assign(serialized_mat.begin(), serialized_mat.end());

        result.artifacts.push_back(std::move(mat_artifact));
        asset_map.sub_assets.push_back({mat_sub_id, tryengine::fmt::format("{}", mat_sub_id)});
    }

    return material_guids;
}

void ProcessTextures(ImportResult& result, const tg3_model* m, uint64_t main_uuid, ModelAssetMap& asset_map) {
    if (!m)
        return;

    for (uint32_t i = 0; i < m->textures_count; ++i) {
        const tg3_texture& gltf_tex = m->textures[i];

        tryengine::resources::VkFilter min_filter = tryengine::resources::VkFilter::VK_FILTER_LINEAR;
        tryengine::resources::VkFilter mag_filter = tryengine::resources::VkFilter::VK_FILTER_LINEAR;
        tryengine::resources::VkSamplerMipmapMode mipmap_mode =
            tryengine::resources::VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR;

        tryengine::resources::VkSamplerAddressMode wrap_u =
            tryengine::resources::VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
        tryengine::resources::VkSamplerAddressMode wrap_v =
            tryengine::resources::VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
        // В glTF 2.0 нет wrap_w, используем безопасный дефолт REPEAT
        tryengine::resources::VkSamplerAddressMode wrap_w =
            tryengine::resources::VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;

        if (gltf_tex.sampler >= 0 && gltf_tex.sampler < static_cast<int32_t>(m->samplers_count)) {
            const tg3_sampler& sampler = m->samplers[gltf_tex.sampler];

            if (sampler.min_filter != -1) {
                min_filter = MapGltfMinFilter(sampler.min_filter);
                mipmap_mode = MapGltfMipmapMode(sampler.min_filter);
            }
            if (sampler.mag_filter != -1)
                mag_filter = MapGltfMagFilter(sampler.mag_filter);
            wrap_u = MapGltfWrap(sampler.wrap_s);
            wrap_v = MapGltfWrap(sampler.wrap_t);
        }

        if (gltf_tex.source < 0 || gltf_tex.source >= static_cast<int32_t>(m->images_count))
            continue;

        const tg3_image& gltf_img = m->images[gltf_tex.source];

        eastl::string base_name = gltf_tex.name.len > 0
                                      ? eastl::string(gltf_tex.name.data, gltf_tex.name.len)
                                      : (gltf_img.name.len > 0 ? eastl::string(gltf_img.name.data, gltf_img.name.len)
                                                               : tryengine::fmt::format("Texture_{}", i));

        const uint8_t* raw_bytes = nullptr;
        size_t byte_count = 0;

        if (gltf_img.buffer_view >= 0 && gltf_img.buffer_view < static_cast<int32_t>(m->buffer_views_count)) {
            const tg3_buffer_view& bv = m->buffer_views[gltf_img.buffer_view];
            if (bv.buffer >= 0 && bv.buffer < static_cast<int32_t>(m->buffers_count)) {
                raw_bytes = m->buffers[bv.buffer].data.data + bv.byte_offset;
                byte_count = bv.byte_length;
            }
        } else if (gltf_img.image.data && gltf_img.image.count > 0) {
            raw_bytes = gltf_img.image.data;
            byte_count = gltf_img.image.count;
        }

        if (!raw_bytes || byte_count == 0)
            continue;

        bool is_srgb = false;
        for (uint32_t mat_idx = 0; mat_idx < m->materials_count; ++mat_idx) {
            const tg3_material& mat = m->materials[mat_idx];

            if (mat.pbr_metallic_roughness.base_color_texture.index == static_cast<int32_t>(i) ||
                mat.emissive_texture.index == static_cast<int32_t>(i)) {
                is_srgb = true;
                break;
            }
        }

        tryengine::resources::VkCompareOp compare_op = tryengine::resources::VkCompareOp::VK_COMPARE_OP_ALWAYS;
        bool enable_anisotropy = false;
        bool enable_compare = false;
        float mip_lod_bias = 0.0f;
        float max_anisotropy = 1.0f;
        float min_lod = 0.0f;
        float max_lod = 0.0f;

        TextureProcessSettings settings;
        settings.format = is_srgb ? tryengine::resources::TextureFormat::VK_FORMAT_BC7_SRGB_BLOCK
                                  : tryengine::resources::TextureFormat::VK_FORMAT_BC7_UNORM_BLOCK;

        settings.sampler = tryengine::resources::SamplerDesc{
            min_filter,        mag_filter,     mipmap_mode,  wrap_u,         wrap_v,  wrap_w, compare_op,
            enable_anisotropy, enable_compare, mip_lod_bias, max_anisotropy, min_lod, max_lod};

        tryengine::Result<eastl::vector<uint8_t>> process_result;

        if (gltf_img.as_is && gltf_img.width > 0 && gltf_img.height > 0) {
            process_result =
                TextureProcessor::ProcessFromRawPixels(raw_bytes, gltf_img.width, gltf_img.height, settings);
        } else {
            eastl::span input_bytes(raw_bytes, byte_count);
            process_result = TextureProcessor::ProcessFromEncodedMemory(input_bytes, settings);
        }

        if (!process_result.has_value())
            LogError("Failed to process texture {}: {}", base_name.c_str(), process_result.error().Message());

        ProducedArtifact artifact;
        artifact.bytes = eastl::move(*process_result);
        artifact.sub_guid = tryengine::core::random::CombineID(main_uuid, base_name);
        artifact.target = ArtifactTarget::Runtime;
        result.artifacts.push_back(artifact);
        asset_map.sub_assets.push_back({artifact.sub_guid, tryengine::fmt::format("{}.tex", artifact.sub_guid)});
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

            int pos_idx = FindAttribute(prim, "POSITION");
            int norm_idx = FindAttribute(prim, "NORMAL");
            int uv_idx = FindAttribute(prim, "TEXCOORD_0");
            int color_idx = FindAttribute(prim, "COLOR_0");
            int joint_idx = FindAttribute(prim, "JOINTS_0");
            int weight_idx = FindAttribute(prim, "WEIGHTS_0");

            raw_input.positions.data =
                GetAccessorData(m, pos_idx, raw_input.positions.stride, raw_input.positions.count);
            raw_input.normals.data = GetAccessorData(m, norm_idx, raw_input.normals.stride, raw_input.normals.count);
            raw_input.uvs.data = GetAccessorData(m, uv_idx, raw_input.uvs.stride, raw_input.uvs.count);
            raw_input.colors.data = GetAccessorData(m, color_idx, raw_input.colors.stride, raw_input.colors.count);
            raw_input.joints.data = GetAccessorData(m, joint_idx, raw_input.joints.stride, raw_input.joints.count);
            raw_input.weights.data = GetAccessorData(m, weight_idx, raw_input.weights.stride, raw_input.weights.count);

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
                raw_input.indices.data =
                    GetAccessorData(m, prim.indices, raw_input.indices.stride, raw_input.indices.count);
                const tg3_accessor& idx_acc = m->accessors[prim.indices];
                if (idx_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT)
                    raw_input.indices.format = tryengine::resources::IndexFormat::UInt16;
                else if (idx_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT)
                    raw_input.indices.format = tryengine::resources::IndexFormat::UInt32;
                else if (idx_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE)
                    raw_input.indices.format = tryengine::resources::IndexFormat::UInt8;
            }

            // --- Запекание меша ---
            MeshProcessSettings process_settings;
            process_settings.auto_select_format = true;

            auto process_result = MeshProcessor::ProcessPrimitive(raw_input, process_settings);
            if (!process_result.has_value()) {
                LogError("Failed to process mesh primitive {}: {}", prim_name.c_str(),
                         process_result.error().Message());
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
            const das::float3 col0(static_cast<float>(gltf_node.matrix[0]), static_cast<float>(gltf_node.matrix[1]),
                                   static_cast<float>(gltf_node.matrix[2]));
            const das::float3 col1(static_cast<float>(gltf_node.matrix[4]), static_cast<float>(gltf_node.matrix[5]),
                                   static_cast<float>(gltf_node.matrix[6]));
            const das::float3 col2(static_cast<float>(gltf_node.matrix[8]), static_cast<float>(gltf_node.matrix[9]),
                                   static_cast<float>(gltf_node.matrix[10]));

            // Позиция находится в 4-м столбце (индексы 12, 13, 14)
            node_data.local_transform.position =
                das::float3(static_cast<float>(gltf_node.matrix[12]), static_cast<float>(gltf_node.matrix[13]),
                            static_cast<float>(gltf_node.matrix[14]));

            // Масштаб — это длины базисных векторов
            const float scale_x = std::sqrt(col0.x * col0.x + col0.y * col0.y + col0.z * col0.z);
            const float scale_y = std::sqrt(col1.x * col1.x + col1.y * col1.y + col1.z * col1.z);
            const float scale_z = std::sqrt(col2.x * col2.x + col2.y * col2.y + col2.z * col2.z);
            node_data.local_transform.scale = das::float3(scale_x, scale_y, scale_z);

            // Нормализуем столбцы для получения чистой матрицы вращения 3x3
            const das::float3 rot0 =
                scale_x > 0.0f ? das::float3(col0.x / scale_x, col0.y / scale_x, col0.z / scale_x) : col0;
            const das::float3 rot1 =
                scale_y > 0.0f ? das::float3(col1.x / scale_y, col1.y / scale_y, col1.z / scale_y) : col1;
            const das::float3 rot2 =
                scale_z > 0.0f ? das::float3(col2.x / scale_z, col2.y / scale_z, col2.z / scale_z) : col2;

            // glTF хранит матрицу по столбцам для column-vector соглашения, а движок работает
            // с row-vector матрицами: столбцы glTF становятся строками матрицы вращения.
            das::float3x3 rot_mat;
            rot_mat.m[0] = rot0;
            rot_mat.m[1] = rot1;
            rot_mat.m[2] = rot2;

            node_data.local_transform.rotation = das::quat_from_float3x3(rot_mat);
        } else {
            node_data.local_transform.position =
                das::float3(static_cast<float>(gltf_node.translation[0]), static_cast<float>(gltf_node.translation[1]),
                            static_cast<float>(gltf_node.translation[2]));

            // glTF и daslang одинаково хранят кватернион как [x, y, z, w]
            node_data.local_transform.rotation =
                das::float4(static_cast<float>(gltf_node.rotation[0]), static_cast<float>(gltf_node.rotation[1]),
                            static_cast<float>(gltf_node.rotation[2]), static_cast<float>(gltf_node.rotation[3]));

            node_data.local_transform.scale =
                das::float3(static_cast<float>(gltf_node.scale[0]), static_cast<float>(gltf_node.scale[1]),
                            static_cast<float>(gltf_node.scale[2]));
        }
        for (uint32_t c = 0; c < gltf_node.children_count; ++c)
            node_data.children_indices.push_back(gltf_node.children[c]);

        if (gltf_node.mesh >= 0 && gltf_node.mesh < static_cast<int32_t>(m->meshes_count)) {
            const tg3_mesh& gltf_mesh = m->meshes[gltf_node.mesh];
            const auto& prim_guids = mesh_primitive_guids[gltf_node.mesh];

            if (gltf_mesh.primitives_count == 1) {
                const tg3_primitive& prim = gltf_mesh.primitives[0];

                node_data.mesh_id = prim_guids.empty() ? 0 : prim_guids[0];
                node_data.material_id = (prim.material >= 0 && prim.material < (int32_t) material_guids.size())
                                            ? material_guids[prim.material]
                                            : 0;  // 0 == движковый материал по умолчанию

            } else if (gltf_mesh.primitives_count > 1) {
                for (uint32_t p = 0; p < gltf_mesh.primitives_count; ++p) {
                    const tg3_primitive& prim = gltf_mesh.primitives[p];

                    ModelNodeData virtual_child;
                    virtual_child.name = tryengine::fmt::format("{}_prim_{}", node_data.name, p);
                    virtual_child.mesh_id = (p < prim_guids.size()) ? prim_guids[p] : 0;
                    virtual_child.material_id = (prim.material >= 0 && prim.material < (int32_t) material_guids.size())
                                                    ? material_guids[prim.material]
                                                    : 0;
                    // Виртуальная нода наследует локальный трансформ ноды-владельца через
                    // идентичность (нулевой трансформ), т.к. её родитель уже несёт трансформ модели.

                    const auto virtual_index = static_cast<int32_t>(asset_map.nodes.size());
                    asset_map.nodes.push_back(
                        std::move(virtual_child));  // safe: no live reference into the vector right now
                    node_data.children_indices.push_back(virtual_index);
                }
            }
        }

        asset_map.nodes[i] = std::move(node_data);  // write back once, after all growth for this node
    }

    const int32_t scene_idx = m->default_scene >= 0 ? m->default_scene : 0;
    if (scene_idx < (int32_t) m->scenes_count) {
        const tg3_scene& scene = m->scenes[scene_idx];
        for (uint32_t i = 0; i < scene.nodes_count; ++i)
            asset_map.scene_roots.push_back(scene.nodes[i]);
    }
}

// ---------------------------------------------------------------------------
// Prefab-ready view of a model (editor artifact only).
//
// The ModelAssetMap keeps local transforms of the glTF nodes. A prefab, on the
// other hand, is a flat pre-order list without runtime ids: the whole hierarchy
// is baked into root-relative TRS, because DECS does not compose transforms
// between entities. The list stays in artifacts/editor and is never packed:
// the game spawns prefabs, not glTF nodes.
// ---------------------------------------------------------------------------

constexpr uint32_t kPrefabRootParent = 0xFFFFFFFFu;

struct PrefabFlatNode {
    uint32_t local_id = 0;
    uint32_t parent_local_id = kPrefabRootParent;
    int32_t node_index = -1;  // -1 for the synthetic root
    das::float4x4 world;
};

void FlattenModelNodes(const ModelAssetMap& map, eastl::vector<PrefabFlatNode>& out) {
    out.clear();
    if (map.nodes.empty())
        return;

    const int32_t node_count = static_cast<int32_t>(map.nodes.size());
    eastl::vector<int32_t> roots;
    for (int32_t index : map.scene_roots)
        if (index >= 0 && index < node_count)
            roots.push_back(index);

    if (roots.empty()) {
        // У модели без default scene корни — все ноды, на которые никто не ссылается.
        eastl::vector<bool> is_child(static_cast<size_t>(node_count), false);
        for (const ModelNodeData& node : map.nodes)
            for (int32_t child : node.children_indices)
                if (child >= 0 && child < node_count)
                    is_child[static_cast<size_t>(child)] = true;
        for (int32_t i = 0; i < node_count; ++i)
            if (!is_child[static_cast<size_t>(i)])
                roots.push_back(i);
    }
    if (roots.empty())
        return;

    // У префаба ровно один корень. Если в glTF их несколько, корень синтетический:
    // так инвариант формата соблюдается без изменения самой иерархии.
    eastl::vector<bool> visited(static_cast<size_t>(node_count), false);

    auto visit = [&](auto&& self, int32_t node_index, uint32_t parent_local, das::float4x4 parent_world) -> void {
        if (node_index < 0 || node_index >= node_count || visited[static_cast<size_t>(node_index)])
            return;
        visited[static_cast<size_t>(node_index)] = true;

        const ModelNodeData& node = map.nodes[static_cast<size_t>(node_index)];
        const das::float4x4 local = das::float4x4_compose(node.local_transform.position, node.local_transform.rotation,
                                                          node.local_transform.scale);
        const das::float4x4 world = das::float4x4_mul(parent_world, local);

        const uint32_t local_id = static_cast<uint32_t>(out.size());
        PrefabFlatNode flat;
        flat.local_id = local_id;
        flat.parent_local_id = parent_local;
        flat.node_index = node_index;
        flat.world = world;
        out.push_back(flat);

        for (int32_t child : node.children_indices)
            self(self, child, local_id, world);
    };

    if (roots.size() == 1) {
        das::float4x4 identity;
        das::float4x4_identity(identity);
        visit(visit, roots[0], kPrefabRootParent, identity);
    } else {
        PrefabFlatNode root;
        root.local_id = 0;
        root.parent_local_id = kPrefabRootParent;
        root.node_index = -1;
        das::float4x4_identity(root.world);
        out.push_back(root);

        das::float4x4 identity;
        das::float4x4_identity(identity);
        for (int32_t index : roots)
            visit(visit, index, 0, identity);
    }
}

eastl::string_view ModelStem(eastl::string_view path) {
    const size_t slash = path.find_last_of("\\/");
    if (slash != eastl::string_view::npos)
        path.remove_prefix(slash + 1);
    const size_t dot = path.find_last_of('.');
    if (dot != eastl::string_view::npos)
        path = path.substr(0, dot);
    return path;
}

ConfigField MakePrefabField(eastl::string name, eastl::string type_name, tryengine::resources::ConfigValue value) {
    ConfigField field;
    field.name = eastl::move(name);
    field.type_name = eastl::move(type_name);
    field.value = eastl::move(value);
    return field;
}

ConfigField MakeStringField(const char* name, eastl::string value) {
    return MakePrefabField(name, "string", tryengine::resources::ConfigValue(eastl::move(value)));
}

eastl::string BuildPrefabText(const ModelAssetMap& map, eastl::string_view asset_path) {
    using tryengine::resources::ConfigArray;
    using tryengine::resources::ConfigFloat3;
    using tryengine::resources::ConfigFloat4;
    using tryengine::resources::ConfigObject;
    using tryengine::resources::ConfigValue;
    using tryengine::resources::WriteConfig;

    eastl::vector<PrefabFlatNode> flat;
    FlattenModelNodes(map, flat);
    if (flat.empty())
        return {};

    // Всё, что находится в префабе, задаётся относительно корня: спавн добавляет
    // только мировую позицию. Обратная матрица корня убирает его собственный
    // трансформ, чтобы корневая сущность стояла в нуле.
    das::float4x4 root_inverse;
    das::float4x4_identity(root_inverse);
    const float determinant = das::float4x4_det(flat[0].world);
    if (std::abs(determinant) > 1e-8f)
        root_inverse = das::float4x4_inverse(flat[0].world);

    ConfigArray entities;
    for (const PrefabFlatNode& node : flat) {
        ConfigObject entity;
        entity.fields.push_back(MakePrefabField("localId", "u32", ConfigValue(node.local_id)));
        entity.fields.push_back(MakePrefabField("parentLocalId", "u32", ConfigValue(node.parent_local_id)));

        eastl::string tag;
        uint64_t mesh_id = 0;
        uint64_t material_id = 0;
        if (node.node_index >= 0) {
            const ModelNodeData& source = map.nodes[static_cast<size_t>(node.node_index)];
            tag = source.name.empty() ? tryengine::fmt::format("Node_{}", node.node_index) : source.name;
            mesh_id = source.mesh_id;
            material_id = source.material_id;
        } else {
            tag = eastl::string(ModelStem(asset_path).data(), ModelStem(asset_path).size());
        }

        entity.fields.push_back(MakeStringField("tag", eastl::move(tag)));

        const das::float4x4 relative = das::float4x4_mul(node.world, root_inverse);
        das::float3 position;
        das::float4 rotation;
        das::float3 scale;
        das::float4x4_decompose(relative, position, rotation, scale);

        entity.fields.push_back(MakePrefabField("Transform_position", "f32x3",
                                                ConfigValue(ConfigFloat3{position.x, position.y, position.z})));
        entity.fields.push_back(MakePrefabField(
            "Transform_rotation", "f32x4", ConfigValue(ConfigFloat4{rotation.x, rotation.y, rotation.z, rotation.w})));
        entity.fields.push_back(
            MakePrefabField("Transform_scale", "f32x3", ConfigValue(ConfigFloat3{scale.x, scale.y, scale.z})));

        if (mesh_id != 0) {
            entity.fields.push_back(MakePrefabField("meshGuid", "u64", ConfigValue(mesh_id)));
            if (material_id != 0) {
                ConfigObject material;
                material.fields.push_back(MakeStringField("path", tryengine::fmt::format("guid:{}", material_id)));
                entity.fields.push_back(
                    MakePrefabField("materialRef", "MaterialRef",
                                    ConfigValue(eastl::make_unique<ConfigObject>(eastl::move(material)))));
            }
        }

        entities.elements.push_back(
            MakePrefabField("", "PrefabEntity", ConfigValue(eastl::make_unique<ConfigObject>(eastl::move(entity)))));
    }

    ConfigObject root;
    root.fields.push_back(MakePrefabField("version", "u32", ConfigValue(uint32_t{1})));
    root.fields.push_back(
        MakeStringField("name", eastl::string(ModelStem(asset_path).data(), ModelStem(asset_path).size())));
    root.fields.push_back(MakeStringField("configType", {}));
    root.fields.push_back(MakeStringField("configRow", {}));
    root.fields.push_back(MakePrefabField("entities", "PrefabEntity[]",
                                          ConfigValue(eastl::make_unique<ConfigArray>(eastl::move(entities)))));

    return WriteConfig(root);
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
        // Мета — вход импорта. Неизвестные поля игнорируем (схема настроек может новеть),
        // но битую мету не подменяем дефолтной: иначе молча сменится guid ассета.
        auto parsed_meta =
            tryengine::resources::Json::DeserializePartial<AssetMeta<GltfImportSettings>>(ctx.meta_bytes);
        if (!parsed_meta.has_value())
            co_return tryengine::core::Error("Failed to parse AssetMeta");

        meta = eastl::move(*parsed_meta);
    } else {
        meta = GenerateDefaultMeta();
    }
    result.main_guid = meta.header.guid;

    ModelAssetMap asset_map;
    tinygltf3::Model model;
    tinygltf3::ErrorStack errors;

    tg3_error_code err = tinygltf3::parse(model, errors, ctx.source_bytes.data(), ctx.source_bytes.size());

    if (err != TG3_OK)
        co_return tryengine::core::Error("Failed to parse gltf model");

    const tg3_model* m = model.get();

    eastl::vector<uint64_t> material_guids = ProcessMaterials(result, m, meta.header.guid, asset_map);
    ProcessTextures(result, m, meta.header.guid, asset_map);
    eastl::vector<eastl::vector<uint64_t>> mesh_primitive_guids = ProcessMeshes(result, m, meta.header.guid, asset_map);
    ProcessNodes(m, asset_map, mesh_primitive_guids, material_guids);

    ProducedArtifact map_artifact;
    auto asset_map_bytes = tryengine::resources::Json::Serialize(asset_map);
    if (!asset_map_bytes.has_value())
        co_return tryengine::core::Error("Failed to serialize ModelAssetMap");

    map_artifact.bytes = *asset_map_bytes;
    map_artifact.sub_guid = static_cast<uint32_t>(tryengine::EditorArtifactGuid::AssetMap);
    map_artifact.target = ArtifactTarget::Editor;

    result.artifacts.push_back(std::move(map_artifact));

    // Плоская композиция для drag&drop/спавна модели в редакторе. Живёт только в
    // artifacts/editor: рантайм спавнит префабы и этот артефакт в пак не попадает.
    eastl::string prefab_text = BuildPrefabText(asset_map, ctx.path);
    if (!prefab_text.empty()) {
        ProducedArtifact prefab_artifact;
        prefab_artifact.sub_guid = static_cast<uint32_t>(tryengine::EditorArtifactGuid::Prefab);
        prefab_artifact.target = ArtifactTarget::Editor;
        prefab_artifact.bytes.assign(prefab_text.begin(), prefab_text.end());
        result.artifacts.push_back(std::move(prefab_artifact));
    }

    // Список пересобираем с нуля: мета уже прочитана из файла, и при реимпорте append
    // задублировал бы все sub_assets.
    meta.header.sub_assets.clear();
    for (const auto& [key, _] : asset_map.sub_assets)
        meta.header.sub_assets.push_back(key);

    auto meta_bytes = tryengine::resources::Json::Serialize(meta);
    if (meta_bytes.has_value())
        result.meta_bytes = *meta_bytes;
    else
        co_return tryengine::core::Error("Failed to serialize AssetMeta");

    LogCritical("END IMPORTING GLTF");

    co_return result;
}

}  // namespace tryeditor
