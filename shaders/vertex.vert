#version 450

layout(location = 0) in vec3 VertexPos;
layout(location = 1) in vec4 VertexColor;
layout(location = 2) in vec2 VertexTexCoord;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 TexCoord;

layout(std140, set = 1, binding = 0) uniform UniformBuffer {
    mat4 mvp; // 64 байта
};

void main() {
    // gl_Position = proj * view * model * vec4(VertexPos, 1.0);
    // gl_Position = vec4(VertexPos, 1.0);
    gl_Position = mvp * vec4(VertexPos, 1.0);

    fragColor = VertexColor;
    TexCoord = VertexTexCoord;
}
