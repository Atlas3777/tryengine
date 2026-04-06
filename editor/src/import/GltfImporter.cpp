#include "editor/import/GltfImporter.hpp"

#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <glm/fwd.hpp>
#include <iostream>
#include <random>

#include "editor/meta/ModelAssetMap.hpp"
#include "engine/resources/Types.hpp"
#include "glm/detail/type_quat.hpp"
#include "tiny_gltf_v3.h"

namespace editor {

// Вспомогательная функция для получения сырого указателя и шага (stride) из аксессора
static const uint8_t* GetAccessorData(const tg3_model* m, int accessor_index, uint32_t& out_stride,
                                      uint32_t& out_count) {
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

// Вспомогательная функция для поиска нужного атрибута в примитиве
static int FindAttribute(const tg3_primitive& prim, const char* name) {
    for (uint32_t i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].key.len > 0 &&
            strncmp(prim.attributes[i].key.data, name, prim.attributes[i].key.len) == 0) {
            return prim.attributes[i].value;
        }
    }
    return -1;
}

// Смещения (salt) для генерации ID, чтобы ID материала не пересекся с ID меша
constexpr uint64_t MESH_SALT = 0x10000000;
constexpr uint64_t MATERIAL_SALT = 0x20000000;

// Детерминированная хеш-функция
uint64_t CombineID(uint64_t main_uuid, uint32_t sub_index, uint64_t salt) {
    uint64_t id = main_uuid ^ salt;
    return id ^ (static_cast<uint64_t>(sub_index) + 0x9e3779b9 + (id << 6) + (id >> 2));
}

void SaveMeshBinary(const std::filesystem::path& path, const engine::resources::MeshData& data) {
    std::ofstream os(path, std::ios::binary);
    uint32_t v_count = data.vertexBuffer.size();
    uint32_t i_count = data.indexBuffer.size();

    os.write(reinterpret_cast<const char*>(&v_count), sizeof(uint32_t));
    os.write(reinterpret_cast<const char*>(&i_count), sizeof(uint32_t));
    os.write(reinterpret_cast<const char*>(data.vertexBuffer.data()), v_count * sizeof(engine::resources::Vertex));
    os.write(reinterpret_cast<const char*>(data.indexBuffer.data()), i_count * sizeof(uint32_t));
}

bool GltfImporter::GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                                    const std::filesystem::path& artifactDir, const std::filesystem::path& cacheDir,
                                    const std::filesystem::path& projectAssetsDir) {
    bool extractMaterials = false;
    AssetHeader header = ReadIdentification(metaPath);

    // Используем новые структуры
    ModelAssetMap asset_map;
    asset_map.main_guid = header.main_uuid;

    std::ifstream file(assetPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> glbData(size);
    if (!file.read(reinterpret_cast<char*>(glbData.data()), size))
        return false;

    tinygltf3::Model model;
    tinygltf3::ErrorStack errors;
    tg3_parse_options opts;
    tg3_parse_options_init(&opts);

    tg3_error_code err = tinygltf3::parse(model, errors, glbData.data(), glbData.size(),
                                          assetPath.parent_path().string().c_str(), &opts);
    if (err != TG3_OK) {
        std::cerr << "Failed to parse GLB: " << assetPath << "\n";
        return false;
    }

    const tg3_model* m = model.get();

    // 2. ИЗВЛЕЧЕНИЕ МАТЕРИАЛОВ (в основную файловую систему проекта .temat)
    std::vector<uint64_t> material_guids(m->materials_count, 0);
    if (extractMaterials) {
        std::filesystem::path matDir = projectAssetsDir / "Materials" / assetPath.stem();
        std::filesystem::create_directories(matDir);

        for (uint32_t i = 0; i < m->materials_count; ++i) {
            const tg3_material& gltf_mat = m->materials[i];
            uint64_t mat_id = CombineID(header.main_uuid, i, MATERIAL_SALT);
            material_guids[i] = mat_id;

            std::string mat_name = gltf_mat.name.len > 0 ? std::string(gltf_mat.name.data, gltf_mat.name.len)
                                                         : "Material_" + std::to_string(i);

            std::filesystem::path matPath = matDir / (mat_name + ".temat");
            std::filesystem::path matMetaPath = matDir / (mat_name + ".temat.meta");

            // TODO: Сериализовать параметры материала (albedo, roughness из gltf_mat.pbr_metallic_roughness) в matPath

            // Создаем meta файл для материала, чтобы AssetDatabase его подхватила
            AssetHeader matHeader;
            matHeader.main_uuid = mat_id;
            // WriteIdentification(matMetaPath, matHeader);
        }
    }

    // 3. ПАРСИНГ МЕШЕЙ И ПРИМИТИВОВ (в артефакты .bin)
    std::vector<std::vector<uint64_t>> mesh_primitive_guids(m->meshes_count);
    uint32_t global_primitive_counter = 0;

    for (uint32_t i = 0; i < m->meshes_count; ++i) {
        const tg3_mesh& gltf_mesh = m->meshes[i];
        for (uint32_t p = 0; p < gltf_mesh.primitives_count; ++p) {
            const tg3_primitive& prim = gltf_mesh.primitives[p];
            engine::resources::MeshData engine_mesh;

            // Ищем нужные атрибуты
            int pos_idx = FindAttribute(prim, "POSITION");
            int norm_idx = FindAttribute(prim, "NORMAL");
            int uv_idx = FindAttribute(prim, "TEXCOORD_0");
            int color_idx = FindAttribute(prim, "COLOR_0");


            // --- ПАРСИНГ ВЕРШИН ---
            if (pos_idx >= 0) {
                uint32_t pos_stride, v_count;
                const uint8_t* pos_data = GetAccessorData(m, pos_idx, pos_stride, v_count);

                engine_mesh.vertexBuffer.resize(v_count);

                // Нормали
                uint32_t norm_stride = 0, norm_count = 0;
                const uint8_t* norm_data = GetAccessorData(m, norm_idx, norm_stride, norm_count);

                // UV
                uint32_t uv_stride = 0, uv_count = 0;
                const uint8_t* uv_data = GetAccessorData(m, uv_idx, uv_stride, uv_count);

                //Color
                uint32_t color_stride = 0, color_count = 0;
                const uint8_t* color_data = GetAccessorData(m, color_idx, color_stride, color_count);
                const tg3_accessor* color_acc = (color_idx >= 0) ? &m->accessors[color_idx] : nullptr;

                for (uint32_t v = 0; v < v_count; ++v) {
                    engine::resources::Vertex& vertex = engine_mesh.vertexBuffer[v];

                    // Читаем позиции
                    const float* pos = reinterpret_cast<const float*>(pos_data + (v * pos_stride));
                    vertex.x = pos[0];
                    vertex.y = pos[1];
                    vertex.z = pos[2];
                    // vertex.a = pos[3];

                    // Читаем нормали
                    if (norm_data) {
                        const float* norm = reinterpret_cast<const float*>(norm_data + (v * norm_stride));
                        vertex.nx = norm[0];
                        vertex.ny = norm[1];
                        vertex.nz = norm[2];
                    } else {
                        vertex.nx = 0.0f;
                        vertex.ny = 1.0f;
                        vertex.nz = 0.0f;
                    }

                    // Читаем UV
                    if (uv_data) {
                        const float* uv = reinterpret_cast<const float*>(uv_data + (v * uv_stride));
                        vertex.u = uv[0];
                        vertex.v = uv[1];
                    } else {
                        vertex.u = 0.0f;
                        vertex.v = 0.0f;
                    }

                    //COLOR
                    if (color_data && color_acc) {
                        const float* col = reinterpret_cast<const float*>(color_data + (v * color_stride));

                        // Проверяем тип данных (VEC3 или VEC4)
                        if (color_acc->type == TG3_TYPE_VEC3) {
                            vertex.r = col[0];
                            vertex.g = col[1];
                            vertex.b = col[2];
                            vertex.a = 1.0f; // По умолчанию непрозрачный
                        } else if (color_acc->type == TG3_TYPE_VEC4) {
                            vertex.r = col[0];
                            vertex.g = col[1];
                            vertex.b = col[2];
                            vertex.a = col[3];
                        }
                    } else {
                        // Если цвета нет, ставим белый по умолчанию
                        vertex.r = 1.0f;
                        vertex.g = 1.0f;
                        vertex.b = 1.0f;
                        vertex.a = 1.0f;
                    }
                }
            }

            // --- ПАРСИНГ ИНДЕКСОВ ---
            if (prim.indices >= 0) {
                uint32_t idx_stride, i_count;
                const uint8_t* idx_data = GetAccessorData(m, prim.indices, idx_stride, i_count);

                if (idx_data) {
                    engine_mesh.indexBuffer.resize(i_count);
                    const tg3_accessor& idx_acc = m->accessors[prim.indices];

                    for (uint32_t id = 0; id < i_count; ++id) {
                        uint32_t index_value = 0;
                        const uint8_t* raw_idx = idx_data + (id * idx_stride);

                        if (idx_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT) {
                            index_value = *reinterpret_cast<const uint16_t*>(raw_idx);
                        } else if (idx_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT) {
                            index_value = *reinterpret_cast<const uint32_t*>(raw_idx);
                        } else if (idx_acc.component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE) {
                            index_value = *reinterpret_cast<const uint8_t*>(raw_idx);
                        }

                        engine_mesh.indexBuffer[id] = index_value;
                    }
                }
            } else {
                // Модель без индексов (DrawArrays)
                uint32_t v_count = engine_mesh.vertexBuffer.size();
                engine_mesh.indexBuffer.resize(v_count);
                for (uint32_t id = 0; id < v_count; ++id) {
                    engine_mesh.indexBuffer[id] = id;
                }
            }

            uint64_t prim_sub_id = CombineID(header.main_uuid, global_primitive_counter, MESH_SALT);
            mesh_primitive_guids[i].push_back(prim_sub_id);

            std::string bin_name = std::to_string(prim_sub_id) + ".bin";
            std::filesystem::path bin_path = artifactDir / bin_name;

            SaveMeshBinary(bin_path, engine_mesh);
            // asset_map.sub_assets[prim_sub_id] = bin_name;
            asset_map.sub_assets.push_back({prim_sub_id, bin_name});

            global_primitive_counter++;
        }
    }

    // 4. ПАРСИНГ ИЕРАРХИИ (Ноды)
    // Изначально копируем все базовые ноды из glTF
    asset_map.nodes.resize(m->nodes_count);

    for (uint32_t i = 0; i < m->nodes_count; ++i) {
        const tg3_node& gltf_node = m->nodes[i];
        ModelNodeData& nav_node = asset_map.nodes[i];

        nav_node.name = gltf_node.name.len > 0 ? std::string(gltf_node.name.data, gltf_node.name.len)
                                               : ("Node_" + std::to_string(i));

        // Трансформ
        if (!gltf_node.has_matrix) {
            nav_node.local_transform.position = {gltf_node.translation[0], gltf_node.translation[1],
                                                 gltf_node.translation[2]};
            nav_node.local_transform.rotation =
                glm::quat(gltf_node.rotation[3], gltf_node.rotation[0], gltf_node.rotation[1], gltf_node.rotation[2]);
            nav_node.local_transform.scale = {gltf_node.scale[0], gltf_node.scale[1], gltf_node.scale[2]};
        }

        // Оригинальные дочерние ноды
        for (uint32_t c = 0; c < gltf_node.children_count; ++c) {
            nav_node.children_indices.push_back(gltf_node.children[c]);
        }

        // --- ЛОГИКА РАСПРЕДЕЛЕНИЯ ПРИМИТИВОВ ---
        if (gltf_node.mesh >= 0 && gltf_node.mesh < (int32_t) m->meshes_count) {
            const tg3_mesh& gltf_mesh = m->meshes[gltf_node.mesh];

            if (gltf_mesh.primitives_count == 1) {
                nav_node.mesh_id = mesh_primitive_guids[gltf_node.mesh][0];
            } else if (gltf_mesh.primitives_count > 1) {
                for (uint32_t p = 0; p < gltf_mesh.primitives_count; ++p) {
                    ModelNodeData virtual_child;
                    virtual_child.name = nav_node.name + "_prim_" + std::to_string(p);
                    virtual_child.mesh_id = mesh_primitive_guids[gltf_node.mesh][p];

                    int32_t virtual_index = static_cast<int32_t>(asset_map.nodes.size());
                    asset_map.nodes.push_back(virtual_child);
                    asset_map.nodes[i].children_indices.push_back(virtual_index);
                }
            }
        }
    }

    // 5. Определение корневых узлов
    int32_t scene_idx = m->default_scene >= 0 ? m->default_scene : 0;
    if (scene_idx < (int32_t) m->scenes_count) {
        const tg3_scene& scene = m->scenes[scene_idx];
        for (uint32_t i = 0; i < scene.nodes_count; ++i) {
            asset_map.scene_roots.push_back(scene.nodes[i]);
        }
    }

    // 6. СОХРАНЕНИЕ КАРТЫ (JSON В ПАПКУ КЭША)
    std::filesystem::path cacheFilePath = cacheDir / "hierarchy.json";
    std::ofstream os(cacheFilePath);
    if (os.is_open()) {
        cereal::JSONOutputArchive archive(os);
        archive(cereal::make_nvp("asset_map", asset_map));
    } else {
        std::cerr << "Failed to open cache file for writing: " << cacheFilePath << std::endl;
        return false;
    }

    return true;
}

AssetHeader GltfImporter::ReadIdentification(const std::filesystem::path& metaPath) {
    AssetHeader header;

    if (!std::filesystem::exists(metaPath)) {
        std::cerr << "[GltfImporter] meta file does not exist: " << metaPath << std::endl;
        return header;
    }

    try {
        std::ifstream is(metaPath);
        if (is.is_open()) {
            cereal::JSONInputArchive archive(is);
            archive(cereal::make_nvp("header", header));
        }
    } catch (const std::exception& e) {
        std::cerr << "[GltfImporter] Failed to read meta file " << metaPath << ". Error: " << e.what() << std::endl;
    }

    return header;
}

uint64_t GltfImporter::GenerateMeta(const std::filesystem::path& asset_path, const std::filesystem::path& meta_path) {
    AssetHeader header;

    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    header.main_uuid = dis(gen);

    try {
        std::ofstream os(meta_path);
        if (os.is_open()) {
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("header", header));
        } else {
            std::cerr << "[GltfImporter] Failed to create meta file: " << meta_path << std::endl;
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "[GltfImporter] Error while saving meta: " << e.what() << std::endl;
        return 0;
    }

    std::cout << "[GltfImporter] Generated new meta for " << asset_path.filename() << " with UUID: " << header.main_uuid
              << std::endl;

    return header.main_uuid;
}

}  // namespace editor