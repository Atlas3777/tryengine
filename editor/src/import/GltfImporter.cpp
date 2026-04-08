#include "editor/import/GltfImporter.hpp"

#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "stb_image_write.h"
#include "stb_image.h"

#include "engine/resources/Types.hpp"
#include "tiny_gltf_v3.h"

namespace tryeditor {

// =============================================================================
// Локальные вспомогательные функции (Скрыты в анонимном namespace)
// =============================================================================
namespace {

constexpr uint64_t MESH_SALT = 0x10000000;
constexpr uint64_t MATERIAL_SALT = 0x20000000;
constexpr uint64_t TEXTURE_SALT = 0x30000000;

// Вспомогательная структура для заголовка текстуры в артефактах
struct TextureHeader {
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t dataSize;
};

uint64_t CombineID(uint64_t main_uuid, uint32_t sub_index, uint64_t salt) {
    uint64_t id = main_uuid ^ salt;
    return id ^ (static_cast<uint64_t>(sub_index) + 0x9e3779b9 + (id << 6) + (id >> 2));
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

int FindAttribute(const tg3_primitive& prim, const char* name) {
    for (uint32_t i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].key.len > 0 &&
            strncmp(prim.attributes[i].key.data, name, prim.attributes[i].key.len) == 0) {
            return prim.attributes[i].value;
        }
    }
    return -1;
}

void SaveMeshBinary(const std::filesystem::path& path, const tryengine::resources::MeshData& data) {
    std::ofstream os(path, std::ios::binary);
    const uint32_t v_count = static_cast<uint32_t>(data.vertexBuffer.size());
    const uint32_t i_count = static_cast<uint32_t>(data.indexBuffer.size());

    os.write(reinterpret_cast<const char*>(&v_count), sizeof(uint32_t));
    os.write(reinterpret_cast<const char*>(&i_count), sizeof(uint32_t));
    os.write(reinterpret_cast<const char*>(data.vertexBuffer.data()), v_count * sizeof(tryengine::resources::Vertex));
    os.write(reinterpret_cast<const char*>(data.indexBuffer.data()), i_count * sizeof(uint32_t));
}


}  // namespace

// =============================================================================
// Публичные интерфейсные методы
// =============================================================================

uint64_t GltfImporter::GenerateMeta(const std::filesystem::path& asset_path, const std::filesystem::path& meta_path) {
    AssetHeader header{};
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

AssetHeader GltfImporter::ReadIdentification(const std::filesystem::path& metaPath) {
    AssetHeader header{};
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

bool GltfImporter::GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                                    const std::filesystem::path& artifactDir, const std::filesystem::path& cacheDir,
                                    const std::filesystem::path& projectAssetsDir) {
    if (!std::filesystem::exists(cacheDir))
        std::filesystem::create_directories(cacheDir);

    AssetHeader header = ReadIdentification(metaPath);

    ModelAssetMap asset_map;
    asset_map.main_guid = header.main_uuid;

    // 1. Чтение файла
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

    // 2. Обработка данных
    ProcessMaterials(m, header.main_uuid, projectAssetsDir, assetPath.stem());

    ProcessTextures(m, header.main_uuid, artifactDir, projectAssetsDir, assetPath.stem(), asset_map);

    std::vector<std::vector<uint64_t>> mesh_primitive_guids;
    ProcessMeshes(m, header.main_uuid, artifactDir, asset_map, mesh_primitive_guids);

    ProcessNodes(m, mesh_primitive_guids, asset_map);

    // 3. Сохранение иерархии в кэш
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

// =============================================================================
// Приватные методы парсинга
// =============================================================================

void GltfImporter::ProcessTextures(const tg3_model* m, uint64_t main_uuid,
                                   const std::filesystem::path& artifactDir,
                                   const std::filesystem::path& projectAssetsDir,
                                   const std::filesystem::path& assetStem,
                                   ModelAssetMap& asset_map) {

    std::filesystem::path texSourceDir = projectAssetsDir / "Textures" / assetStem;
    std::filesystem::create_directories(texSourceDir);

    for (uint32_t i = 0; i < m->images_count; ++i) {
        const tg3_image& gltf_img = m->images[i];
        uint64_t tex_id = CombineID(main_uuid, i, TEXTURE_SALT);

        // 1. ПОЛУЧАЕМ СЖАТЫЕ ДАННЫЕ (PNG/JPG) ИЗ GLB
        const uint8_t* compressed_data = nullptr;
        size_t compressed_size = 0;

        if (gltf_img.image.count > 0) {
            // Если данные уже почему-то скопированы в image
            compressed_data = gltf_img.image.data;
            compressed_size = gltf_img.image.count;
        } else if (gltf_img.buffer_view >= 0) {
            // ТИПИЧНЫЙ СЛУЧАЙ ДЛЯ GLB: берем из buffer_view
            const tg3_buffer_view& bv = m->buffer_views[gltf_img.buffer_view];
            const tg3_buffer& buf = m->buffers[bv.buffer];
            compressed_data = buf.data.data + bv.byte_offset;
            compressed_size = bv.byte_length;
        }

        if (!compressed_data || compressed_size == 0) continue;

        // Определяем расширение (по mime_type или сигнатуре)
        std::string ext = ".png";
        if (gltf_img.mime_type.len > 0) {
            std::string mime(gltf_img.mime_type.data, gltf_img.mime_type.len);
            if (mime == "image/jpeg") ext = ".jpg";
        }

        std::string base_name = gltf_img.name.len > 0
            ? std::string(gltf_img.name.data, gltf_img.name.len)
            : ("Texture_" + std::to_string(i));

        std::filesystem::path assetPath = texSourceDir / (base_name + ext);
        std::filesystem::path metaPath = texSourceDir / (base_name + ext + ".meta");

        // --- А: СОХРАНЯЕМ В ASSETS (оригинальный сжатый файл) ---
        {
            std::ofstream os(assetPath, std::ios::binary);
            os.write(reinterpret_cast<const char*>(compressed_data), compressed_size);
        }

        // --- Б: СОЗДАЕМ META ФАЙЛ ---
        {
            AssetHeader texMeta;
            texMeta.main_uuid = tex_id;
            std::ofstream os(metaPath);
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("header", texMeta));
        }

        // --- В: СОЗДАЕМ АРТЕФАКТ (Декодируем в RGBA для GPU) ---
        int width, height, channels;
        // Разжимаем PNG/JPG в сырые пиксели
        unsigned char* pixels = stbi_load_from_memory(compressed_data, (int)compressed_size,
                                                      &width, &height, &channels, 4); // Принудительно 4 канала (RGBA)

        if (pixels) {
            std::string artName = std::to_string(tex_id) + ".tex";
            std::filesystem::path artPath = artifactDir / artName;
            std::ofstream artOs(artPath, std::ios::binary);

            if (artOs.is_open()) {
                TextureHeader head;
                head.width = (uint32_t)width;
                head.height = (uint32_t)height;
                head.channels = 4;
                head.dataSize = (uint32_t)(width * height * 4);

                artOs.write(reinterpret_cast<const char*>(&head), sizeof(TextureHeader));
                artOs.write(reinterpret_cast<const char*>(pixels), head.dataSize);
            }

            stbi_image_free(pixels);
            asset_map.sub_assets.push_back({tex_id, artName});
        }
    }
}


void GltfImporter::ProcessMaterials(const tg3_model* m, uint64_t main_uuid,
                                    const std::filesystem::path& projectAssetsDir,
                                    const std::filesystem::path& assetStem) {
    bool extract_materials = false;  // Флаг из старого кода
    if (!extract_materials)
        return;

    std::filesystem::path matDir = projectAssetsDir / "Materials" / assetStem;
    std::filesystem::create_directories(matDir);

    for (uint32_t i = 0; i < m->materials_count; ++i) {
        const tg3_material& gltf_mat = m->materials[i];
        uint64_t mat_id = CombineID(main_uuid, i, MATERIAL_SALT);

        std::string mat_name = gltf_mat.name.len > 0 ? std::string(gltf_mat.name.data, gltf_mat.name.len)
                                                     : "Material_" + std::to_string(i);

        std::filesystem::path matPath = matDir / (mat_name + ".temat");
        std::filesystem::path matMetaPath = matDir / (mat_name + ".temat.meta");

        // TODO: Сериализовать параметры материала
        AssetHeader matHeader;
        matHeader.main_uuid = mat_id;
        // WriteIdentification(matMetaPath, matHeader);
    }
}

void GltfImporter::ProcessMeshes(const tg3_model* m, uint64_t main_uuid, const std::filesystem::path& artifactDir,
                                 ModelAssetMap& asset_map,
                                 std::vector<std::vector<uint64_t>>& out_mesh_primitive_guids) {
    out_mesh_primitive_guids.resize(m->meshes_count);
    uint32_t global_primitive_counter = 0;

    for (uint32_t i = 0; i < m->meshes_count; ++i) {
        const tg3_mesh& gltf_mesh = m->meshes[i];
        for (uint32_t p = 0; p < gltf_mesh.primitives_count; ++p) {
            const tg3_primitive& prim = gltf_mesh.primitives[p];
            tryengine::resources::MeshData engine_mesh;

            int pos_idx = FindAttribute(prim, "POSITION");
            int norm_idx = FindAttribute(prim, "NORMAL");
            int uv_idx = FindAttribute(prim, "TEXCOORD_0");
            int color_idx = FindAttribute(prim, "COLOR_0");

            // --- ПАРСИНГ ВЕРШИН ---
            if (pos_idx >= 0) {
                uint32_t pos_stride, v_count;
                const uint8_t* pos_data = GetAccessorData(m, pos_idx, pos_stride, v_count);

                engine_mesh.vertexBuffer.resize(v_count);

                uint32_t norm_stride = 0, norm_count = 0;
                const uint8_t* norm_data = GetAccessorData(m, norm_idx, norm_stride, norm_count);

                uint32_t uv_stride = 0, uv_count = 0;
                const uint8_t* uv_data = GetAccessorData(m, uv_idx, uv_stride, uv_count);

                uint32_t color_stride = 0, color_count = 0;
                const uint8_t* color_data = GetAccessorData(m, color_idx, color_stride, color_count);
                const tg3_accessor* color_acc = (color_idx >= 0) ? &m->accessors[color_idx] : nullptr;

                for (uint32_t v = 0; v < v_count; ++v) {
                    tryengine::resources::Vertex& vertex = engine_mesh.vertexBuffer[v];

                    const float* pos = reinterpret_cast<const float*>(pos_data + (v * pos_stride));
                    vertex.x = pos[0];
                    vertex.y = pos[1];
                    vertex.z = pos[2];

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

                    if (uv_data) {
                        const float* uv = reinterpret_cast<const float*>(uv_data + (v * uv_stride));
                        vertex.u = uv[0];
                        vertex.v = uv[1];
                    } else {
                        vertex.u = 0.0f;
                        vertex.v = 0.0f;
                    }

                    if (color_data && color_acc) {
                        const float* col = reinterpret_cast<const float*>(color_data + (v * color_stride));
                        if (color_acc->type == TG3_TYPE_VEC3) {
                            vertex.r = col[0];
                            vertex.g = col[1];
                            vertex.b = col[2];
                            vertex.a = 1.0f;
                        } else if (color_acc->type == TG3_TYPE_VEC4) {
                            vertex.r = col[0];
                            vertex.g = col[1];
                            vertex.b = col[2];
                            vertex.a = col[3];
                        }
                    } else {
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
                uint32_t v_count = engine_mesh.vertexBuffer.size();
                engine_mesh.indexBuffer.resize(v_count);
                for (uint32_t id = 0; id < v_count; ++id)
                    engine_mesh.indexBuffer[id] = id;
            }

            uint64_t prim_sub_id = CombineID(main_uuid, global_primitive_counter, MESH_SALT);
            out_mesh_primitive_guids[i].push_back(prim_sub_id);

            std::string bin_name = std::to_string(prim_sub_id) + ".bin";
            std::filesystem::path bin_path = artifactDir / bin_name;

            SaveMeshBinary(bin_path, engine_mesh);
            asset_map.sub_assets.push_back({prim_sub_id, bin_name});

            global_primitive_counter++;
        }
    }
}

void GltfImporter::ProcessNodes(const tg3_model* m, const std::vector<std::vector<uint64_t>>& mesh_primitive_guids,
                                ModelAssetMap& asset_map) {
    asset_map.nodes.resize(m->nodes_count);

    for (uint32_t i = 0; i < m->nodes_count; ++i) {
        const tg3_node& gltf_node = m->nodes[i];
        ModelNodeData& nav_node = asset_map.nodes[i];

        nav_node.name = gltf_node.name.len > 0 ? std::string(gltf_node.name.data, gltf_node.name.len)
                                               : ("Node_" + std::to_string(i));

        // --- ИСПРАВЛЕННЫЙ ТРАНСФОРМ ---
        if (gltf_node.has_matrix) {
            // Если трансформ задан матрицей, декомпозируем её
            glm::mat4 mat(1.0f);
            for (int j = 0; j < 16; ++j) {
                mat[j / 4][j % 4] = static_cast<float>(gltf_node.matrix[j]);
            }

            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(mat, nav_node.local_transform.scale, nav_node.local_transform.rotation,
                           nav_node.local_transform.position, skew, perspective);
        } else {
            // Иначе читаем векторы (tiny_gltf заполняет дефолтными значениями если они отсутствуют)
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
                    // Для виртуальной ноды оставляем нулевой локальный трансформ

                    int32_t virtual_index = static_cast<int32_t>(asset_map.nodes.size());
                    asset_map.nodes.push_back(virtual_child);
                    asset_map.nodes[i].children_indices.push_back(virtual_index);
                }
            }
        }
    }

    // Определение корневых узлов
    int32_t scene_idx = m->default_scene >= 0 ? m->default_scene : 0;
    if (scene_idx < (int32_t) m->scenes_count) {
        const tg3_scene& scene = m->scenes[scene_idx];
        for (uint32_t i = 0; i < scene.nodes_count; ++i) {
            asset_map.scene_roots.push_back(scene.nodes[i]);
        }
    }
}

}  // namespace tryeditor