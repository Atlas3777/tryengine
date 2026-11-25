#include "engine/graphics/UiPass.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/RasterState.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/ShaderReflection.hpp"
#include "engine/graphics/UiSystem.hpp"

namespace tryengine::graphics {

namespace {

struct UiPushConstants {
    VkDeviceAddress vertices = 0;
    VkDeviceAddress frame = 0;
};

constexpr uint32_t kKindQuad = 0;
constexpr uint32_t kKindText = 1;
constexpr uint32_t kKindTextured = 2;
constexpr uint32_t kKindBorder = 3;

//! text_id sentinel: the glyphs come from UiSystem::OverlayText(), not from the script table.
constexpr uint32_t kOverlayTextId = 0xFFFFFFFFu;

//! Intersects a script-provided clip rectangle with the render area.
VkRect2D ComposeScissor(const das::float4& scissor, VkExtent2D extent) {
    const int32_t x = std::max(0, static_cast<int32_t>(scissor.x));
    const int32_t y = std::max(0, static_cast<int32_t>(scissor.y));
    const int32_t right = std::min(static_cast<int32_t>(extent.width), static_cast<int32_t>(scissor.x + scissor.z));
    const int32_t bottom = std::min(static_cast<int32_t>(extent.height), static_cast<int32_t>(scissor.y + scissor.w));
    VkRect2D rect{};
    rect.offset = {x, y};
    rect.extent = {static_cast<uint32_t>(std::max(0, right - x)), static_cast<uint32_t>(std::max(0, bottom - y))};
    return rect;
}

}  // namespace

// The pass reads the layout from daslang and the shader from Ui.slang; both are contracts.
static_assert(sizeof(UiPass::UiDrawCmd) == 80, "UiDrawCmd must match [cpp_layout] UiDrawCmd in tryengine/UiTypes.das");
static_assert(offsetof(UiPass::UiDrawCmd, kind) == 0);
static_assert(offsetof(UiPass::UiDrawCmd, text_id) == 4);
static_assert(offsetof(UiPass::UiDrawCmd, material) == 8);
static_assert(offsetof(UiPass::UiDrawCmd, rect) == 16);
static_assert(offsetof(UiPass::UiDrawCmd, color) == 32);
static_assert(offsetof(UiPass::UiDrawCmd, extra) == 48);
static_assert(offsetof(UiPass::UiDrawCmd, scissor) == 64);

static_assert(sizeof(UiPass::UiVertex) == 64, "UiVertex must match Ui.slang");

void UiPass::Setup(RenderGraphBuilder& builder) {
    builder.Write(At(RGTag_v<"ForwardColor">, scope_), RGUsageHint::ColorAttachment);
}

void UiPass::Collect(CollectContext& ctx) {
    cmds_.clear();
    vertices_.clear();
    batches_.clear();
    viewport_ = {0, 0};

    auto* ui = ctx.engine.TryGet<UiSystem>();
    if (ui == nullptr)
        return;
    ui->Tick();
    if (!ui->IsReady())
        return;

    const auto* extent = ctx.cpu_bb.GetAs<VkExtent2D>(RGTag_v<"ViewportSize">);
    if (extent == nullptr || extent->width == 0 || extent->height == 0)
        return;
    viewport_ = *extent;
    frame_data_ = ui->FrameData(das::float2(static_cast<float>(viewport_.width), static_cast<float>(viewport_.height)));

    auto* script = ctx.engine.TryGet<core::ScriptSystem>();
    if (script == nullptr || !script->IsReady()) {
        // During the first load and during a hot reload the script context is intentionally
        // not called: daScript is compiling in the background. Draw the engine-owned status
        // instead, so the window never turns into a frozen black rectangle.
        if (script != nullptr && (script->IsCompiling() || script->IsFailed()))
            EmitScriptStatus(*script, *ui);
        return;
    }

    auto result = script->SimpleReturnUnsafe<"get_ui_draw_cmds", das::Array*>();
    if (!result.has_value() || *result == nullptr || (*result)->size == 0) {
        if (!warned_missing_export_) {
            warned_missing_export_ = true;
            LogInfo(LogCategory::Graphics, "UiPass: 'get_ui_draw_cmds' is missing or empty; the UI draws nothing");
        }
        return;
    }

    const auto* commands = reinterpret_cast<const UiDrawCmd*>((*result)->data);
    cmds_.assign(commands, commands + (*result)->size);
    ExpandDraws(*ui);
}

void UiPass::EmitScriptStatus(core::ScriptSystem& script, UiSystem& ui) {
    const float width = static_cast<float>(viewport_.width);
    const float height = static_cast<float>(viewport_.height);
    if (width <= 0.0f || height <= 0.0f)
        return;

    const das::float4 full(0.0f, 0.0f, width, height);
    UiDrawCmd background{};
    background.kind = kKindQuad;
    background.material = 0;
    background.rect = full;
    background.color = das::float4(0.055f, 0.06f, 0.075f, 1.0f);
    background.scissor = full;
    cmds_.push_back(background);

    const bool failed = script.IsFailed();
    eastl::string message;
    if (failed) {
        message = "Script compilation failed";
        const eastl::string_view error = script.LastError();
        if (!error.empty()) {
            size_t end = 0;
            while (end < error.size() && end < 140 && error[end] != '\n' && error[end] != '\r')
                ++end;
            message += ": ";
            message.append(error.data(), end);
            if (end < error.size())
                message += "...";
        }
    } else {
        message = "Compiling scripts";
    }
    ui.SetOverlayText(message.c_str());

    const float center_x = width * 0.5f;
    const float center_y = height * 0.5f;
    const float radius = 7.0f;
    const float gap = 30.0f;

    uint32_t dot_count = 1;
    if (!failed) {
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        dot_count = static_cast<uint32_t>(seconds * 2.0) % 3u + 1u;
    }

    for (uint32_t i = 0; i < dot_count; ++i) {
        UiDrawCmd dot{};
        dot.kind = kKindQuad;
        dot.material = 0;
        dot.rect =
            das::float4(center_x + (static_cast<float>(i) - static_cast<float>(dot_count - 1) * 0.5f) * gap - radius,
                        center_y - radius, radius * 2.0f, radius * 2.0f);
        dot.color = failed ? das::float4(0.95f, 0.25f, 0.2f, 1.0f) : das::float4(0.95f, 0.95f, 0.95f, 1.0f);
        dot.extra = das::float4(radius, 0.0f, 0.0f, 0.0f);
        dot.scissor = full;
        cmds_.push_back(dot);
    }

    constexpr float kTextPx = 22.0f;
    const das::float2 text_size = ui.Measure(message.c_str(), kTextPx);
    if (text_size.x > 0.0f) {
        UiDrawCmd text{};
        text.kind = kKindText;
        text.text_id = kOverlayTextId;
        text.material = 0;
        text.rect = das::float4(center_x - text_size.x * 0.5f, center_y + radius + 24.0f, text_size.x, text_size.y);
        text.color = failed ? das::float4(1.0f, 0.82f, 0.8f, 1.0f) : das::float4(0.92f, 0.92f, 0.95f, 1.0f);
        text.extra = das::float4(kTextPx, 0.0f, 0.0f, 0.0f);
        text.scissor = full;
        cmds_.push_back(text);
    }

    ExpandDraws(ui);
}

void UiPass::BeginBatch(uint64_t material, const das::float4& scissor) {
    if (!batches_.empty()) {
        Batch& last = batches_.back();
        if (last.material == material && last.scissor.x == scissor.x && last.scissor.y == scissor.y &&
            last.scissor.z == scissor.z && last.scissor.w == scissor.w) {
            return;  // continue the current batch: same material and clip
        }
    }
    Batch batch{};
    batch.first_vertex = static_cast<uint32_t>(vertices_.size());
    batch.material = material;
    batch.scissor = scissor;
    batches_.push_back(batch);
}

void UiPass::ExpandDraws(UiSystem& ui) {
    for (const UiDrawCmd& cmd : cmds_) {
        if (cmd.kind > kKindBorder || cmd.rect.z <= 0.0f || cmd.rect.w <= 0.0f || cmd.scissor.z <= 0.0f ||
            cmd.scissor.w <= 0.0f) {
            continue;
        }

        Material* material = cmd.material == 0 ? ui.GetMaterial() : reinterpret_cast<Material*>(cmd.material);
        if (material == nullptr || !material->shader || material->shader->vertex_shader == VK_NULL_HANDLE) {
            if (!warned_material_) {
                warned_material_ = true;
                LogError("UiPass: draw command references a material that is not ready; skipped");
            }
            continue;
        }

        BeginBatch(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(material)), cmd.scissor);
        Batch& batch = batches_.back();

        switch (cmd.kind) {
            case kKindText:
                EmitText(cmd, ui);
                break;
            case kKindTextured:
                EmitTextured(cmd);
                break;
            case kKindBorder:
                EmitShape(cmd, kKindBorder);
                break;
            default:
                EmitShape(cmd, kKindQuad);
                break;
        }

        batch.vertex_count = static_cast<uint32_t>(vertices_.size()) - batch.first_vertex;
    }
}

void UiPass::AppendQuad(const UiDrawCmd& cmd, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                        float v1, const das::float4& shape, const das::float4& params) {
    if (vertices_.size() + 6 > kMaxVertices) {
        if (!warned_overflow_) {
            warned_overflow_ = true;
            LogError("UiPass: vertex budget {} exceeded; the tail of the draw list is skipped", kMaxVertices);
        }
        return;
    }

    const auto make = [&](float x, float y, float u, float v) {
        UiVertex vertex{};
        vertex.position = das::float2(x, y);
        vertex.uv = das::float2(u, v);
        vertex.color = cmd.color;
        vertex.shape = shape;
        vertex.params = params;
        vertices_.push_back(vertex);
    };

    make(x0, y0, u0, v0);
    make(x1, y0, u1, v0);
    make(x1, y1, u1, v1);
    make(x0, y0, u0, v0);
    make(x1, y1, u1, v1);
    make(x0, y1, u0, v1);
}

void UiPass::EmitShape(const UiDrawCmd& cmd, uint32_t mode) {
    const float width = cmd.rect.z;
    const float height = cmd.rect.w;
    const float radius = std::clamp(cmd.extra.x, 0.0f, std::min(width, height) * 0.5f);
    const float border = mode == kKindBorder ? std::clamp(cmd.extra.y, 0.0f, std::min(width, height) * 0.5f) : 0.0f;
    if (mode == kKindBorder && border <= 0.0f)
        return;

    const das::float4 shape(radius, border, width * 0.5f, height * 0.5f);
    const das::float4 params(static_cast<float>(mode), 0.0f, 0.0f, 0.0f);

    // uv carries local pixel coordinates: the fragment shader rebuilds the SDF from them.
    AppendQuad(cmd, cmd.rect.x, cmd.rect.y, cmd.rect.x + width, cmd.rect.y + height, 0.0f, 0.0f, width, height, shape,
               params);
}

void UiPass::EmitTextured(const UiDrawCmd& cmd) {
    AppendQuad(cmd, cmd.rect.x, cmd.rect.y, cmd.rect.x + cmd.rect.z, cmd.rect.y + cmd.rect.w, 0.0f, 0.0f, 1.0f, 1.0f,
               das::float4(0.0f, 0.0f, 0.0f, 0.0f), das::float4(static_cast<float>(kKindTextured), 0.0f, 0.0f, 0.0f));
}

void UiPass::EmitText(const UiDrawCmd& cmd, UiSystem& ui) {
    const float px = cmd.extra.x;
    if (px <= 0.0f)
        return;

    const char* text = cmd.text_id == kOverlayTextId ? ui.OverlayText() : ui.FrameString(cmd.text_id);
    if (text == nullptr || *text == '\0')
        return;

    const float baseline = cmd.rect.y + ui.Ascent(px);
    float pen_x = cmd.rect.x;

    const char* cursor = text;
    while (*cursor != '\0') {
        uint32_t codepoint = 0;
        cursor = UiSystem::NextCodepoint(cursor, codepoint);

        UiGlyphQuad glyph{};
        if (ui.Glyph(codepoint, px, glyph)) {
            const float x0 = pen_x + glyph.plane_left;
            const float y0 = baseline + glyph.plane_top;
            const das::float4 shape(0.0f, 0.0f, 0.0f, 0.0f);
            const das::float4 params(static_cast<float>(kKindText), glyph.px_per_em, 0.0f, 0.0f);
            AppendQuad(cmd, x0, y0, x0 + (glyph.plane_right - glyph.plane_left),
                       y0 + (glyph.plane_bottom - glyph.plane_top), glyph.u0, glyph.v0, glyph.u1, glyph.v1, shape,
                       params);
        }
        pen_x += ui.Advance(codepoint, px);
    }
}

void UiPass::Execute(RGExecuteContext& ctx) {
    if (batches_.empty() || viewport_.width == 0 || viewport_.height == 0 || vertices_.empty())
        return;

    RGColorAttachment color_attachment{};
    color_attachment.handle = ctx.frame_bb != nullptr ? ctx.frame_bb->Get(RGTag_v<"ForwardColor">) : RGResourceHandle{};
    if (!color_attachment.handle.IsValid())
        return;
    color_attachment.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.store_op = VK_ATTACHMENT_STORE_OP_STORE;

    const VkRect2D render_area{{0, 0}, viewport_};
    ctx.BeginRendering(eastl::span(&color_attachment, 1), nullptr, render_area);

    SetViewportScissor(ctx.cmd_buffer, viewport_);
    ApplyRasterState(ctx.cmd_buffer, {.cull_mode = VK_CULL_MODE_NONE,
                                      .depth_test = false,
                                      .depth_write = false,
                                      .depth_compare = VK_COMPARE_OP_ALWAYS,
                                      .blend_enable = true,
                                      .src_color = VK_BLEND_FACTOR_ONE,
                                      .dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                      .src_alpha = VK_BLEND_FACTOR_ONE,
                                      .dst_alpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA});

    const TransientAllocation vertex_allocation =
        ctx.AllocateTransient(vertices_.data(), sizeof(UiVertex) * vertices_.size(), 16);
    const TransientAllocation frame_allocation = ctx.AllocateTransient(&frame_data_, sizeof(UiFrameData), 16);

    const UiPushConstants push_constants{.vertices = vertex_allocation.bda_address,
                                         .frame = frame_allocation.bda_address};

    const Shader* current_shader = nullptr;
    const Material* current_material = nullptr;
    constexpr uint32_t kMaterialSet = static_cast<uint32_t>(BindingScope::Material);

    for (const Batch& batch : batches_) {
        if (batch.vertex_count == 0)
            continue;

        auto* material = reinterpret_cast<Material*>(batch.material);
        if (material == nullptr || !material->shader)
            continue;

        const Shader& shader = *material->shader;
        if (&shader != current_shader) {
            const VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
            const VkShaderEXT shaders[] = {shader.vertex_shader, shader.fragment_shader};
            vkCmdBindShadersEXT(ctx.cmd_buffer, 2, stages, shaders);
            vkCmdPushConstants(ctx.cmd_buffer, shader.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(UiPushConstants), &push_constants);
            current_shader = &shader;
            current_material = nullptr;
        }

        if (material != current_material) {
            if (shader.descriptor_set_layouts.size() > kMaterialSet && material->descriptor_set != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(ctx.cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader.layout, kMaterialSet, 1,
                                        &material->descriptor_set, 0, nullptr);
            }
            current_material = material;
        }

        const VkRect2D scissor = ComposeScissor(batch.scissor, viewport_);
        vkCmdSetScissorWithCountEXT(ctx.cmd_buffer, 1, &scissor);
        vkCmdDraw(ctx.cmd_buffer, batch.vertex_count, 1, batch.first_vertex, 0);
    }

    ctx.EndRendering();
}

}  // namespace tryengine::graphics
