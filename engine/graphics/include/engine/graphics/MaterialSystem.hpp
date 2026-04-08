#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tryengine::graphics {

// 1. Поддерживаемые типы данных в шейдере
enum class ShaderParamType { Float, Int, Vec2, Vec3, Vec4, Mat3, Mat4 };

// 2. Описание одного параметра (имя, тип, и главное — где он лежит в памяти)
struct MaterialParamDesc {
    std::string name;
    ShaderParamType type;
    uint32_t offset;  // Смещение в байтах (с учетом выравнивания std140)
    uint32_t size;    // Размер в байтах
};

// 3. Схема данных (Layout). Описывает, как данные выглядят для конкретного шейдера
struct MaterialLayout {
    std::vector<MaterialParamDesc> params;
    uint32_t uniform_buffer_size = 0;   // Общий размер буфера со всеми отступами
    uint32_t uniform_binding_slot = 2;  // В какой слот пушить данные (по умолчанию 2)
};

// 4. Текстура и её сэмплер для конкретного слота
struct MaterialTexture {
    uint32_t binding_slot;
    SDL_GPUTexture* texture = nullptr;
    SDL_GPUSampler* sampler = nullptr;
};

// 5. Сам Материал (Asset / Ресурс).
// Это "чертеж". Он один на все объекты, которые используют этот шейдер.
struct Material {
    std::string name;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;  // Главная вещь: скомпилированный стейт
    MaterialLayout layout;

    // Дефолтные значения параметров, чтобы инстанс при создании не был забит мусором
    std::vector<uint8_t> default_uniform_data;
};

// 6. Блок данных для конкретного инстанса
struct MaterialData {
    std::vector<uint8_t> uniform_buffer;  // CPU-копия данных (которую мы будем пушить на GPU)
    std::vector<MaterialTexture> textures;
};

// 7. Экземпляр Материала (Instance).
// Это то, что висит на конкретном объекте. Ссылается на "чертеж" и хранит свои уникальные данные.
struct MaterialInstance {
    Material* material = nullptr;
    MaterialData data;

    // Конструктор, который инициализирует буфер дефолтными значениями из материала
    MaterialInstance(Material* mat) : material(mat) {
        if (mat) {
            data.uniform_buffer = mat->default_uniform_data;
        }
    }

    // --- Удобное API для установки параметров ---

    // Установка текстуры
    void SetTexture(uint32_t slot, SDL_GPUTexture* tex, SDL_GPUSampler* samp) {
        // Ищем, есть ли уже такой слот, если есть - перезаписываем, иначе добавляем
        for (auto& t : data.textures) {
            if (t.binding_slot == slot) {
                t.texture = tex;
                t.sampler = samp;
                return;
            }
        }
        data.textures.push_back({slot, tex, samp});
    }

    // Универсальный метод установки float, vec3, mat4 и т.д.
    template <typename T>
    void SetParam(const std::string& name, const T& value) {
        if (!material)
            return;

        // Ищем параметр в layout'е материала
        for (const auto& param : material->layout.params) {
            if (param.name == name) {
                // Защита от выхода за пределы памяти
                if (param.offset + sizeof(T) <= data.uniform_buffer.size()) {
                    std::memcpy(data.uniform_buffer.data() + param.offset, &value, sizeof(T));
                }
                return;
            }
        }
    }
};

struct MeshRenderer {
    MaterialInstance* material_instance = nullptr;
};

}  // namespace graphics