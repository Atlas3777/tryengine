#pragma once

#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/resources/ResourceHandle.hpp"

namespace tryengine::graphics {
struct Shader;
}

namespace tryeditor {

// Сетка под сценой в окне Scene. Проход только редакторский: ищет пересечение луча
// пикселя с плоскостью Z=0 (мировая "земля", та же, что у drag&drop префабов), поэтому
// сетка бесконечная и не требует геометрии. Глубина точки пересечения приходит из
// фрагментного шейдера, и обычный depth-тест прячет сетку за объектами сцены.
class GridPass final : public tryengine::graphics::IRenderPass {
public:
    explicit GridPass(tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader) : shader_(shader) {}

    const char* Name() const override {
        return "Grid";
    }

    bool IsActive() const override {
        return static_cast<bool>(shader_);
    }

    // Базовая минорная клетка в мировых юнитах. Дальше шейдер сам выбирает LOD:
    // когда между линиями остаётся меньше min_pixels_between_cells пикселей, клетка
    // вырастает на порядок (как в редакторе Blender).
    float cell_size = 1.0f;
    float min_pixels_between_cells = 2.0f;

    // Радиус, на котором сетка гаснет вокруг камеры по плоскости: у горизонта линии
    // сходятся плотнее пикселя, и без фейда они мерцают.
    float grid_size = 200.0f;

    void Setup(tryengine::graphics::RenderGraphBuilder& builder) override;
    void Execute(tryengine::graphics::RGExecuteContext& ctx) override;

private:
    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader_;

    tryengine::graphics::RGResourceHandle color_;
    tryengine::graphics::RGResourceHandle depth_;
};

}  // namespace tryeditor
