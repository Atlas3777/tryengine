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
    int width = 0;
    int height = 0;
    std::string path; // для отладки
};

// Меш — это геометрия + материал (текстура)
struct Mesh {
    SDL_GPUBuffer* vertexBuffer = nullptr;
    SDL_GPUBuffer* indexBuffer = nullptr;
    Uint32 numIndices = 0;
    Texture* texture = nullptr; // Ссылка на текстуру (не копия!)
};

// Игровой объект — это то, что мы ставим на сцену
struct GameObject {
    Mesh* mesh;             // Какой меш рисовать (ссылка)
    glm::vec3 position;     // Где
    glm::vec3 rotation;     // Как повернут (Euler angles)
    glm::vec3 scale;        // Масштаб

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
    glm::mat4 mvp;
};
