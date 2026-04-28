#pragma once
#include <glm/glm.hpp>

namespace tryengine::graphics {

struct RenderProfile {
    // Настройки цвета и тона
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    float exposure = 1.0f;
    float gamma = 2.2f;

    // Специфичные эффекты
    float bloomIntensity = 0.0f;
    glm::vec4 vignetteColor{0.0f, 0.0f, 0.0f, 1.0f};
    float vignetteIntensity = 0.0f;

    // Метод для линейной интерполяции между двумя профилями
    static RenderProfile Lerp(const RenderProfile& a, const RenderProfile& b, float t) {
        RenderProfile res;
        res.clearColor = glm::mix(a.clearColor, b.clearColor, t);
        res.exposure = glm::mix(a.exposure, b.exposure, t);
        res.gamma = glm::mix(a.gamma, b.gamma, t);
        res.bloomIntensity = glm::mix(a.bloomIntensity, b.bloomIntensity, t);
        res.vignetteColor = glm::mix(a.vignetteColor, b.vignetteColor, t);
        res.vignetteIntensity = glm::mix(a.vignetteIntensity, b.vignetteIntensity, t);
        return res;
    }
};

}