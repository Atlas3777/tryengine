#pragma once

#include <EASTL/vector.h>
#include <daScript/misc/vectypes.h>

#include "engine/graphics/UiSystem.hpp"
#include "engine/graphics/rg/IRenderPass.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::core {
class ScriptSystem;
}

namespace tryengine::graphics {

struct Material;

//! Game UI pass. Scripts describe the frame; this pass reads one flat command list, expands
//! it into vertices (glyph quads are built here, not in daslang) and blends them into the
//! scene colour of its scope.
//!
//! It must be registered after OpaquePass in the same scope: it is the last writer of
//! ForwardColor there, and the readers (ImGui, BlitPass) follow it through the graph.
class UiPass final : public IRenderPass {
public:
    static constexpr size_t kMaxVertices = 1'000'000;

    explicit UiPass(ScopeId scope) : scope_(scope) {}

    [[nodiscard]] const char* Name() const override {
        return "Ui";
    }

    void Collect(CollectContext& ctx) override;
    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RGExecuteContext& ctx) override;

    //! Mirror of [cpp_layout] UiDrawCmd in tryengine/UiTypes.das.
    struct UiDrawCmd {
        uint32_t kind;
        uint32_t text_id;
        uint64_t material;
        das::float4 rect;
        das::float4 color;
        das::float4 extra;
        das::float4 scissor;
    };

    //! One vertex; alignas(16) because the shader reads the array through BDA as std430.
    struct alignas(16) UiVertex {
        das::float2 position;
        das::float2 uv;
        das::float4 color;
        das::float4 shape;
        das::float4 params;
    };

private:
    struct Batch {
        uint32_t first_vertex = 0;
        uint32_t vertex_count = 0;
        uint64_t material = 0;
        das::float4 scissor{0.0f, 0.0f, 0.0f, 0.0f};
    };

    void ExpandDraws(UiSystem& ui);
    void BeginBatch(uint64_t material, const das::float4& scissor);
    void AppendQuad(const UiDrawCmd& cmd, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                    float v1, const das::float4& shape, const das::float4& params);
    void EmitShape(const UiDrawCmd& cmd, uint32_t mode);
    void EmitText(const UiDrawCmd& cmd, UiSystem& ui);
    //! C++ fallback when there is no script frame: loading dots or the compile error. The
    //! script context either does not exist yet (first load) or is being recompiled, so the
    //! usual `get_ui_draw_cmds` cannot be called.
    void EmitScriptStatus(core::ScriptSystem& script, UiSystem& ui);
    void EmitTextured(const UiDrawCmd& cmd);

    ScopeId scope_;
    eastl::vector<UiDrawCmd> cmds_;
    eastl::vector<UiVertex> vertices_;
    eastl::vector<Batch> batches_;
    UiFrameData frame_data_{};
    VkExtent2D viewport_{0, 0};
    bool warned_missing_export_ = false;
    bool warned_overflow_ = false;
    bool warned_material_ = false;
};

}  // namespace tryengine::graphics
