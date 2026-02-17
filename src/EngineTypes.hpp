#pragma once
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

// Меш — это геометрия + материал (текстура)
struct Mesh {
    SDL_GPUBuffer* vertexBuffer = nullptr;
    SDL_GPUBuffer* indexBuffer = nullptr;
    Uint32 numIndices = 0;
    Texture* texture = nullptr;  // Ссылка на текстуру (не копия!)
};

struct TransformComponent {
    Mesh* mesh;          // Какой меш рисовать (ссылка)
    glm::vec3 position;  // Где
    glm::vec3 rotation;  // Как повернут (Euler angles)
    glm::vec3 scale;     // Масштаб

    // Матрица модели считается на лету
    glm::mat4 GetModelMatrix() {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), {1, 0, 0});
        model = glm::rotate(model, glm::radians(rotation.y), {0, 1, 0});
        model = glm::rotate(model, glm::radians(rotation.z), {0, 0, 1});
        model = glm::scale(model, scale);
        return model;
    }
};

struct UniformBufferObject {
    glm::mat4 model;  // Локальные -> Мировые
    glm::mat4 view;   // Мировые -> Камера
    glm::mat4 proj;   // Камера -> Экран
    glm::mat4 normalMatrix;
};

struct LightUniforms {
    glm::vec4 lightPos;    // Позиция света (w не используем)
    glm::vec4 lightColor;  // Цвет света (w можно использовать как интенсивность)
    glm::vec4 viewPos;     // Позиция камеры (понадобится для бликов/Specular, добавим сразу)
};
