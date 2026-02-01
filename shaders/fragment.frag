#version 450

layout(location = 0) in vec3 inFragPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

void main() {
    // --- НАСТРОЙКИ СВЕТА (потом вынесем в Uniform) ---
    vec3 lightColor = vec3(1.0, 1.0, 1.0); // Белый свет
    float ambientStrength = 0.3; // Сила фонового света (30%)

    // --- AMBIENT (Фон) ---
    vec3 ambient = ambientStrength * lightColor;

    // --- ИТОГОВЫЙ ЦВЕТ ---
    // Берем цвет текстуры
    vec4 objectColor = texture(texSampler, inTexCoord) * inColor;
    
    // Умножаем свет на цвет объекта
    // (ambient) * objectColor.rgb -> пока только фон
    vec3 result = ambient * objectColor.rgb;

    outColor = vec4(result, objectColor.a);
}
