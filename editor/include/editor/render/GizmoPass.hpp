#pragma once

#include <EASTL/vector.h>
#include <cstdint>
#include <daScript/misc/vectypes.h>

#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/resources/ResourceHandle.hpp"

namespace tryengine::graphics {
struct Shader;
}

namespace tryeditor {

//! Зеркало daslang-структуры `GizmoDrawState` ([cpp_layout] в Gizmo.das): режим,
//! пространство, hover и экранно-постоянный размер задаёт скрипт, пасс только превращает
//! это в треугольники.
struct GizmoDrawState {
    das::float3 position;  // пивот гизмо в мировом пространстве
    float size;            // длина одного юнита гизмо в мире (экранно-постоянная, из скрипта)
    das::float4 rotation;  // базис гизмо: мировой (identity) или локальный (поворот сущности)
    int32_t mode;          // 0 = перемещение, 1 = поворот, 2 = масштаб
    int32_t hover;         // ручка под мышью, -1 если нет
    int32_t active;        // ручка, которую тянут, -1 если нет
    int32_t visible;       // 0 прячет гизмо
};

//! Одна вершина процедурного меша гизмо. Члены float4 дают одинаковый std430-шаг (48 байт)
//! на обеих сторонах без сюрпризов с padding'ом.
struct GizmoVertex {
    das::float4 position;  // xyz в мире
    das::float4 normal;    // xyz мировая нормаль
    das::float4 color;     // rgba без премультипликации
};

//! Редакторский гизмо трансформа. Строит стрелки/плоскости/кольца на CPU и рисует их
//! поверх SceneColor без depth-теста: гизмо всегда сверху, а picking делает Gizmo.das
//! аналитически, не читая этот пасс обратно.
class GizmoPass final : public tryengine::graphics::IRenderPass {
public:
    explicit GizmoPass(tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader) : shader_(shader) {}

    const char* Name() const override {
        return "Gizmo";
    }

    bool IsActive() const override {
        return static_cast<bool>(shader_);
    }

    void Collect(tryengine::graphics::CollectContext& ctx) override;
    void Setup(tryengine::graphics::RenderGraphBuilder& builder) override;
    void Execute(tryengine::graphics::RGExecuteContext& ctx) override;

private:
    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader_;
    GizmoDrawState state_{};
    eastl::vector<GizmoVertex> vertices_;

    tryengine::graphics::RGResourceHandle color_;
};

}  // namespace tryeditor
