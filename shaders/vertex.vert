#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

// Передаем во фрагментный шейдер
layout(location = 0) out vec3 outFragPos; // Позиция фрагмента в мире
layout(location = 1) out vec3 outNormal;  // Нормаль в мире
layout(location = 2) out vec4 outColor;   // Цвет вершины (если есть)
layout(location = 3) out vec2 outTexCoord;// UV

layout(set = 1, binding = 0) uniform UniformBuffer {
    mat4 model;
    mat4 view;
    mat4 proj;
};

void main() {
    // 1. Позиция вершины в мире
    vec4 worldPos = model * vec4(inPos, 1.0);
    outFragPos = vec3(worldPos);

    // 2. Нормаль в мире
    // ВАЖНО: Если ты будешь неравномерно скейлить (например 1, 5, 1), 
    // то нормали сломаются. Правильно: mat3(transpose(inverse(model))) * inNormal
    // Но для начала сойдет и так:
    outNormal = mat3(model) * inNormal; 

    // 3. Просто пробрасываем цвет и UV
    outColor = inColor;
    outTexCoord = inTexCoord;

    // 4. Финальная позиция на экране
    gl_Position = proj * view * worldPos;
}
