#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 outFragPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;
layout(location = 3) out vec2 outTexCoord;

// Всё в одном блоке, строго set 1, binding 0
layout(set = 1, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    mat4 model;
    mat4 normalMatrix;
} ubo;

void main() {
    vec4 worldPos = ubo.model * vec4(inPos, 1.0);
    outFragPos = vec3(worldPos);
    outNormal = normalize(mat3(ubo.normalMatrix) * inNormal);
    
    outColor = inColor;
    outTexCoord = inTexCoord;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
