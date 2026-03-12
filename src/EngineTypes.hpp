#pragma once
#include <SDL3/SDL_gpu.h>
#include <entt/entity/entity.hpp>
// #include <entt/entity/fwd.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

// Структура вершины для шейдера
struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    float u, v;
};

// Обертка над текстурой GPU
struct Texture {
    SDL_GPUTexture* handle = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    std::string path;  // для отладки
};

struct DirtyComponent {};
// Меш — это геометрия + материал (текстура)
struct Mesh {
    SDL_GPUBuffer* vertexBuffer = nullptr;
    SDL_GPUBuffer* indexBuffer = nullptr;
    Uint32 numIndices = 0;
    Texture* texture = nullptr;  // Ссылка на текстуру (не копия!)
    glm::vec3 localMin;
    glm::vec3 localMax;
};

struct AABBComponent {
    glm::vec3 worldMin;
    glm::vec3 worldMax;
};

struct MeshComponent {
    Mesh* mesh;
};

struct HierarchyComponent {
    entt::entity parent = entt::null;
    int depth = 0;  // 0 для корней, 1 для детей и т.д.
};

struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity
    glm::vec3 scale{1.0f};

    glm::mat4 worldMatrix{1.0f};

    glm::mat4 GetLocalMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model *= glm::mat4_cast(rotation);
        model = glm::scale(model, scale);
        return model;
    }
};

// Добавим простой компонент имени, если у тебя его еще нет
struct TagComponent {
    std::string tag;
    TagComponent() = default;
    TagComponent(const TagComponent&) = default;
    TagComponent(const std::string& t) : tag(t) {}
};
struct CameraComponent {
    float fov = 70.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float sensitivity = 0.08f;
    float speed = 3.0f;

    // Вычисляемые векторы (обновляются системой)
    glm::vec3 front = {0.0f, 0.0f, -1.0f};
    glm::vec3 up = {0.0f, 1.0f, 0.0f};
    glm::vec3 right = {1.0f, 0.0f, 0.0f};

    glm::mat4 viewMatrix = glm::mat4(1.0f);
};
struct EditorCameraTag {};
struct SelectedTag {};
// Теперь материал владеет конвейером (pipeline) и текстурой
struct Material {
    SDL_GPUGraphicsPipeline* pipeline = nullptr;  // Шейдерная программа
    Texture* diffuseTexture = nullptr;            // Основная текстура

    // Параметры материала, которые мы можем менять
    struct Properties {
        glm::vec4 colorTint = glm::vec4(1.0f);  // Множитель цвета
        float specularStrength = 0.5f;
        float shininess = 32.0f;
        float padding[2];  // Для выравнивания в GPU (16 байт)
    } props;
};

struct LightUniforms {
    glm::vec4 lightPos;    // Позиция света (w не используем)
    glm::vec4 lightColor;  // Цвет света (w можно использовать как интенсивность)
    glm::vec4 viewPos;     // Позиция камеры (понадобится для бликов/Specular, добавим сразу)
};
enum class MouseButton : uint8_t {
    Left = 1,    // Соответствует SDL_BUTTON_LEFT
    Middle = 2,  // Соответствует SDL_BUTTON_MIDDLE
    Right = 3,   // Соответствует SDL_BUTTON_RIGHT
    X1 = 4,
    X2 = 5,
    Count
};
