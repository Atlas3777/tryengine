#version 450

layout(location = 0) in vec3 inFragPos; // Позиция пикселя в мире
layout(location = 1) in vec3 inNormal;  // Нормаль
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

// Наш новый блок света
layout(set = 3, binding = 0) uniform LightBlock {
    vec4 lightPos;
    vec4 lightColor;
    vec4 viewPos;
} light;

void main() {
    // 1. AMBIENT (Фон)
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * light.lightColor.rgb;

    // 2. DIFFUSE (Рассеянный)
    // Обязательно нормализуем, так как интерполяция могла испортить длину вектора
    vec3 norm = normalize(inNormal);
    // Вектор от пикселя к лампочке
    vec3 lightDir = normalize(light.lightPos.xyz - inFragPos);
    
    // Скалярное произведение. max(..., 0.0) убирает отрицательные значения (свет сзади)
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * light.lightColor.rgb;

    // --- ИТОГ ---
    vec4 objectColor = texture(texSampler, inTexCoord) * inColor;
    
    // Складываем свет (Ambient + Diffuse) и умножаем на цвет предмета
    vec3 result = (ambient + diffuse) * objectColor.rgb;

    outColor = vec4(result, objectColor.a);
}
