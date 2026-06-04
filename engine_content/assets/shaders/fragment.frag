#version 450

layout(location = 0) in vec3 inFragPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

// Структура одной лампы (ровно 32 байта, кратно 16 — идеальное std140 выравнивание)
struct LightData {
    vec4 position_type; // xyz = Позиция/Направление, w = Тип (0.0 = Dir, 1.0 = Point)
    vec4 color_radius;  // rgb = Цвет * Интенсивность, w = Радиус затухания
};

// Структура глобального освещения — СЛОТ 3 (layout set = 3)
layout(set = 3, binding = 0) uniform LightBlock {
    vec4 ambientColor;  // 16 байт
    vec4 viewPos;       // 16 байт
    uint lightCount;    // 4 байта (последующие 12 байт автоматически заполнятся паддингом до границы массива)
    LightData lights[8];// 8 * 32 байта
} lightBlock;

void main() {
    vec3 norm = normalize(inNormal);
    vec3 viewDir = normalize(lightBlock.viewPos.xyz - inFragPos);

    float specularStrength = 0.5;
    float shininess = 32.0;

    // Эмбиент берем глобальный, переданный из движка (он не должен дублироваться от каждой лампы)
    vec3 totalAmbient = lightBlock.ambientColor.rgb;
    vec3 totalDiffuse = vec3(0.0);
    vec3 totalSpecular = vec3(0.0);

    uint activeLights = min(lightBlock.lightCount, 8);

    for (uint i = 0; i < activeLights; ++i) {
        vec3 lightPosOrDir = lightBlock.lights[i].position_type.xyz;
        float lightType = lightBlock.lights[i].position_type.w;
        vec3 lightColor = lightBlock.lights[i].color_radius.rgb;
        float radius = lightBlock.lights[i].color_radius.w;

        vec3 lightDir;
        float attenuation = 1.0;

        if (lightType > 0.5) {
            // --- POINT LIGHT ---
            float distance = length(lightPosOrDir - inFragPos);
            if (distance > radius) continue; // Источник слишком далеко

            // Линейный спад освещения до нуля на границе радиуса
            attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
            lightDir = normalize(lightPosOrDir - inFragPos);
        } else {
            // --- DIRECTIONAL LIGHT ---
            // Направление направленного света (излучается "в сторону", инвертируем для расчета)
            lightDir = normalize(-lightPosOrDir);
        }

        // --- DIFFUSE ---
        float diff = max(dot(norm, lightDir), 0.0);
        totalDiffuse += diff * lightColor * attenuation;

        // --- SPECULAR ---
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
        totalSpecular += specularStrength * spec * lightColor * attenuation;
    }

    vec3 lightingResult = totalAmbient + totalDiffuse + totalSpecular;

    vec4 texColor = texture(texSampler, inTexCoord);
    outColor = vec4(lightingResult, 1.0) * texColor * inColor;
}