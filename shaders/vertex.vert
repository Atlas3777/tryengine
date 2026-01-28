#version 450

layout(location = 0) in vec3 VertexPos;
layout(location = 1) in vec4 VertexColor;

layout(location = 0) out vec4 fragColor;

layout(std140, set = 3, binding = 0) uniform UniformBufferVertex {
    float srcAspect;
};

void main() {
    gl_Position = vec4(VertexPos.x * srcAspect, VertexPos.yz, 1.0);
    fragColor = VertexColor;
}
