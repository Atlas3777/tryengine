#pragma once

#include <daScript/misc/vectypes.h>
#include <vk_mem_alloc.h>

#include "engine/graphics/FrameContext.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/resources/ResourceHandle.hpp"

namespace tryengine::graphics {
class VulkanDevice;
class OpaquePass;
}  // namespace tryengine::graphics

namespace tryeditor {

struct Mouse {
    das::float2 pos = {0, 0};
    bool pressed = false;
    bool is_down = false;
};

class PickingPass final : public tryengine::graphics::IRenderPass {
public:
    explicit PickingPass(tryengine::graphics::VulkanDevice& device,
                         tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader);
    ~PickingPass() override;

    PickingPass(const PickingPass&) = delete;
    PickingPass& operator=(const PickingPass&) = delete;

    const char* Name() const override { return "Picking"; }
    // Collect должен запускаться каждый кадр, чтобы вычитывать результаты из GPU readback буфера
    bool IsActive() const override { return true; }

    void Collect(tryengine::graphics::CollectContext& ctx) override;
    void Setup(tryengine::graphics::RenderGraphBuilder& builder) override;
    void Execute(tryengine::graphics::RGExecuteContext& ctx) override;

private:
    struct Readback {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mapped_ptr = nullptr;
        bool pending = false;
    };

    tryengine::graphics::VulkanDevice& device_;
    Readback readback_[tryengine::graphics::MAX_FRAMES_IN_FLIGHT];
    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader_;

    const tryengine::graphics::OpaquePass* opaque_ = nullptr;
    bool requested_ = false;
    uint32_t mouse_x_ = 0;
    uint32_t mouse_y_ = 0;
    uint32_t frame_index_ = 0;

    tryengine::graphics::RGResourceHandle picking_;
    tryengine::graphics::RGResourceHandle depth_;
    tryengine::graphics::RGResourceHandle readback_handle_;
};

}  // namespace tryeditor