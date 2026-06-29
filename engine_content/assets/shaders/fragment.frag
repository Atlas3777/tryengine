#version 450

layout(location = 0) in vec3 inFragPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

struct PointLight {
    vec4 position; // xyz = позиция, w = радиус затухания
    vec4 color;    // rgb = цвет,     w = интенсивность
};

layout(set = 2, binding = 1) readonly buffer LightBuffer {
    PointLight lights[];
};

layout(set = 3, binding = 0) uniform GlobalLightBlock {
    vec4 ambientColor;
    vec4 viewPos;
} globalLight;

void main() {
    vec3 normal = normalize(inNormal);
    vec3 viewDir = normalize(globalLight.viewPos.xyz - inFragPos);

    vec3 ambient = globalLight.ambientColor.rgb;

    vec3 diffuseAccum = vec3(0.0);
    vec3 specularAccum = vec3(0.0);

    // 3. Цикл расчета источников света
    for (int i = 0; i < lights.length(); i++) {
        PointLight light = lights[i];

        // Вектор направления к свету и дистанция
        vec3 lightVec = light.position.xyz - inFragPos;
        float dist = length(lightVec);
        float radius = light.position.w;

        // Если лампа слишком далеко — она не влияет на фрагмент
        if (dist > radius || radius <= 0.0) continue;

        vec3 lightDir = lightVec / dist; // Быстрая нормализация

        // Диффузное (диффузное отражение Ламберта)
        float diff = max(dot(normal, lightDir), 0.0);

        // Блик (Blinn-Phong)
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 32.0);

        // Мягкое линейное затухание (1.0 в центре, 0.0 на границе радиуса)
        float attenuation = 1.0 - (dist / radius);

        // Интенсивность (w-компонента цвета)
        float intensity = light.color.w;

        // Накапливаем свет от текущей лампы
        diffuseAccum += light.color.rgb * diff * intensity * attenuation;
        specularAccum += light.color.rgb * spec * intensity * attenuation * 0.5;
    }

    // 4. Текстурирование с защитой
    vec4 texColor = texture(texSampler, inTexCoord);
    // Если текстура пустая/черная, берем цвет вершины. Если и он пуст — берем белый.
    if (length(texColor.rgb) == 0.0) {
        texColor = (length(inColor.rgb) > 0.0) ? inColor : vec4(1.0);
    }

    // 5. Финальный цвет
    vec3 finalLighting = ambient + diffuseAccum + specularAccum;
    outColor = vec4(finalLighting, 1.0) * texColor;
}