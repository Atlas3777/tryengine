#version 450

layout(location = 0) in vec3 inFragPos; // Позиция пикселя в мире
layout(location = 1) in vec3 inNormal;  // Нормаль
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

layout(set = 3, binding = 0) uniform LightBlock {
    vec4 lightPos;
    vec4 lightColor;
    vec4 viewPos; // Позиция камеры (ты её уже передаешь из C++)
} light;

void main() {
    // Настройки материала (в будущем вынесем в отдельную структуру Material)
    float ambientStrength = 0.1;
    float specularStrength = 0.5; // Сила блика (яркость)
    float shininess = 32.0;       // "Блеск". Чем выше (64, 128, 256), тем меньше и четче блик.

    // --- 1. AMBIENT (Фон) ---
    vec3 ambient = ambientStrength * light.lightColor.rgb;

    // --- 2. DIFFUSE (Рассеянный) ---
    vec3 norm = normalize(inNormal);
    vec3 lightDir = normalize(light.lightPos.xyz - inFragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * light.lightColor.rgb;

    // --- 3. SPECULAR (Блики) ---
    // Вектор взгляда: от позиции фрагмента к камере
    vec3 viewDir = normalize(light.viewPos.xyz - inFragPos);
    
    // Вектор отражения: lightDir указывает НА лампочку, 
    // поэтому берем обратный (-lightDir), чтобы он падал НА поверхность.
    vec3 reflectDir = reflect(-lightDir, norm);
    
    // Считаем угол между взглядом и отражением
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    vec3 specular = specularStrength * spec * light.lightColor.rgb;

    // --- ИТОГ ---
    vec4 objectColor = texture(texSampler, inTexCoord) * inColor;
    
    // Суммируем все три компонента
    vec3 result = (ambient + diffuse + specular) * objectColor.rgb;

    outColor = vec4(result, objectColor.a);
}
