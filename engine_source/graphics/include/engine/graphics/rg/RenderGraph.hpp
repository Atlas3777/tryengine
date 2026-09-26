#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <volk.h>
#include <vk_mem_alloc.h>

#include <EASTL/fixed_vector.h>
#include <EASTL/functional.h>
#include <EASTL/hash_map.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstring>
#include <memory>
#include <algorithm>

#include "engine/core/Assert.hpp"
#include "engine/core/Log.hpp"
#include "engine/graphics/rg/RGTag.hpp"

namespace tryengine::graphics {

class VulkanDevice;

struct RGResourceHandle {
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;
    uint32_t index = kInvalid;
    uint16_t version = 0;

    [[nodiscard]] constexpr bool IsValid() const { return index != kInvalid; }
    friend bool operator==(const RGResourceHandle&, const RGResourceHandle&) = default;
};

enum class RGResourceType : uint8_t {
    Texture,
    Buffer,
    ExternalTexture,
    ExternalBuffer
};

struct RGTextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 1;
    uint32_t mips = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    eastl::string debug_name;
};

struct RGBufferDesc {
    uint64_t size = 0;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags alloc_flags = 0;
    eastl::string debug_name;
};

enum class RGAccessType : uint8_t {
    Read,
    Write,
    Create
};

enum class RGUsageHint : uint32_t {
    None                   = 0,
    ColorAttachment        = 1 << 0,
    DepthStencilAttachment = 1 << 1,
    DepthStencilReadOnly   = 1 << 2,
    ShaderRead             = 1 << 3,
    StorageRead            = 1 << 4,
    StorageWrite           = 1 << 5,
    StorageReadWrite       = StorageRead | StorageWrite,
    TransferSrc            = 1 << 6,
    TransferDst            = 1 << 7,
    Present                = 1 << 8
};

inline constexpr RGUsageHint operator|(RGUsageHint a, RGUsageHint b) {
    return static_cast<RGUsageHint>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr bool operator&(RGUsageHint a, RGUsageHint b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

struct RGResourceAccess {
    RGTag tag = 0;
    RGResourceHandle handle;
    RGAccessType access = RGAccessType::Read;
    RGUsageHint usage_hint = RGUsageHint::None;
};

/// Результат суб-аллокации транзиентной памяти для кадра
struct TransientAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceAddress bda_address = 0; // Включает смещение (base_bda + offset)
    uint8_t* mapped_ptr = nullptr;

    [[nodiscard]] bool IsValid() const { return buffer != VK_NULL_HANDLE; }
};

class Blackboard {
public:
    void Put(RGTag tag, RGResourceHandle h) { entries_[tag] = h; }

    [[nodiscard]] RGResourceHandle Get(RGTag tag) const {
        auto it = entries_.find(tag);
        if (it != entries_.end())
            return it->second;

        TRY_ASSERT(false, "Ресурс {} не найден в Blackboard", GetRGTagName(tag));
        return RGResourceHandle{};
    }

    void Clear() { entries_.clear(); }

private:
    eastl::hash_map<RGTag, RGResourceHandle> entries_;
};

class CPUBlackboard {
public:
    CPUBlackboard() = default;

    void Put(RGTag tag, const void* data, size_t size_bytes) {
        auto& buffer = entries_[tag];
        buffer.resize(size_bytes);
        if (data && size_bytes > 0) {
            std::memcpy(buffer.data(), data, size_bytes);
        }
    }

    template <typename T>
    void Put(RGTag tag, const T& data) {
        Put(tag, &data, sizeof(T));
    }

    [[nodiscard]] eastl::optional<eastl::span<const uint8_t>> GetSpan(RGTag tag) const {
        auto it = entries_.find(tag);
        if (it != entries_.end()) {
            return eastl::span(it->second.data(), it->second.size());
        }

        LogError("CPUBlackboard: Запись для тега {} не найдена.", tag);
        return eastl::nullopt;
    }

    [[nodiscard]] const void* GetData(RGTag tag) const {
        auto span = GetSpan(tag);
        return span ? span->data() : nullptr;
    }

    [[nodiscard]] size_t GetSize(RGTag tag) const {
        auto span = GetSpan(tag);
        return span ? span->size() : 0;
    }

    template <typename T>
    [[nodiscard]] const T* GetAs(RGTag tag) const {
        auto span = GetSpan(tag);
        if (span && span->size() >= sizeof(T)) {
            return reinterpret_cast<const T*>(span->data());
        }
        LogError("CPUBlackboard: Размер данных меньше запрашиваемой структуры.");
        return nullptr;
    }

    [[nodiscard]] bool Has(RGTag tag) const { return entries_.find(tag) != entries_.end(); }

    void Clear() { entries_.clear(); }

private:
    eastl::hash_map<RGTag, eastl::vector<uint8_t>> entries_;
};

class RenderGraph;

struct RGColorAttachment {
    RGResourceHandle handle;
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clear_value = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkImageView resolve_view = VK_NULL_HANDLE;
    VkAttachmentLoadOp resolve_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp resolve_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
};

struct RGDepthStencilAttachment {
    RGResourceHandle handle;
    VkAttachmentLoadOp depth_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp depth_store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clear_value = {.depthStencil = {1.0f, 0}};
    VkAttachmentLoadOp stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
};

class RGExecuteContext {
public:
    VkDevice device = VK_NULL_HANDLE;
    VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
    Blackboard* frame_bb = nullptr;
    CPUBlackboard* cpu_bb = nullptr;

    [[nodiscard]] VkImage GetTexture(RGResourceHandle h) const;
    [[nodiscard]] VkImageView GetImageView(RGResourceHandle h) const;
    [[nodiscard]] VkBuffer GetBuffer(RGResourceHandle h) const;

    /// Начинает Vulkan 1.3+ Dynamic Rendering проход (vkCmdBeginRendering)
    void BeginRendering(
        eastl::span<const RGColorAttachment> color_attachments,
        const RGDepthStencilAttachment* depth_attachment = nullptr,
        VkRect2D render_area = {},
        uint32_t layer_count = 1
    );

    /// Завершает Dynamic Rendering проход (vkCmdEndRendering)
    void EndRendering();

    /// Выделяет память в транзиентном линейном буфере текущего кадра
    [[nodiscard]] TransientAllocation AllocateTransient(const void* data, VkDeviceSize size, VkDeviceSize alignment = 256) const;

    void SetGraph(RenderGraph* graph) { graph_ = graph; }

private:
    RenderGraph* graph_ = nullptr;
};

class PassNodeBase {
public:
    virtual ~PassNodeBase() = default;
    virtual void Execute(RGExecuteContext& ctx) = 0;

    eastl::string name;
    eastl::fixed_vector<RGResourceAccess, 16> accesses;
    bool has_side_effect = false;
    bool culled = false;

    eastl::fixed_vector<uint32_t, 8> deps;

    // Сгенерированные Synchronization 2 барьеры для вызова перед началом пасса
    eastl::vector<VkImageMemoryBarrier2> image_barriers;
    eastl::vector<VkBufferMemoryBarrier2> buffer_barriers;
};

template <typename PassData>
class PassNode final : public PassNodeBase {
public:
    PassData data{};
    eastl::function<void(RGExecuteContext&, const PassData&)> execute_fn;

    void Execute(RGExecuteContext& ctx) override {
        if (execute_fn) {
            execute_fn(ctx, data);
        }
    }
};

class RenderGraphBuilder {
public:
    explicit RenderGraphBuilder(RenderGraph& graph, PassNodeBase* pass) : graph_(graph), pass_(pass) {}

    RGResourceHandle CreateTexture(eastl::string_view dbg_name, const RGTextureDesc& desc);
    RGResourceHandle CreateBuffer(eastl::string_view dbg_name, const RGBufferDesc& desc);

    RGResourceHandle Read(RGResourceHandle h, RGUsageHint usage = RGUsageHint::ShaderRead);
    RGResourceHandle Write(RGResourceHandle h, RGUsageHint usage = RGUsageHint::StorageWrite);

    void Export(RGTag tag, RGResourceHandle h);
    RGResourceHandle Import(RGTag tag);

    CPUBlackboard& GetCPUBlackboard();
    const CPUBlackboard& GetCPUBlackboard() const;

    void MarkSideEffect() { pass_->has_side_effect = true; }

private:
    RenderGraph& graph_;
    PassNodeBase* pass_;
};

struct RGVirtualResource {
    eastl::string name;
    RGResourceType type = RGResourceType::Texture;
    RGTextureDesc texture_desc;
    RGBufferDesc buffer_desc;

    VkImage physical_image = VK_NULL_HANDLE;
    VkImageView physical_image_view = VK_NULL_HANDLE;
    VmaAllocation image_allocation = VK_NULL_HANDLE;

    VkBuffer physical_buffer = VK_NULL_HANDLE;
    VmaAllocation buffer_allocation = VK_NULL_HANDLE;

    // Отслеживание текущего состояния ресурса в графе
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 current_stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 current_access = VK_ACCESS_2_NONE;

    PassNodeBase* producer_pass = nullptr;
    uint32_t ref_count = 0;
    bool externally_exported = false;
};

class RGResourcePool {
public:
    explicit RGResourcePool(VulkanDevice& device);
    ~RGResourcePool();

    struct PooledTexture {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        RGTextureDesc desc;
        bool in_use = false;
    };

    struct PooledBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        RGBufferDesc desc;
        bool in_use = false;
    };

    PooledTexture AcquireTexture(const RGTextureDesc& desc);
    PooledBuffer AcquireBuffer(const RGBufferDesc& desc);

    void ReleaseAll();
    void DestroyAll();

private:
    VulkanDevice& device_;
    eastl::vector<PooledTexture> texture_pool_;
    eastl::vector<PooledBuffer> buffer_pool_;
};

// Внутренние структуры кадрового линейного аллокатора
struct TransientPage {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    uint8_t* mapped_ptr = nullptr;
    VkDeviceSize size = 0;
    VkDeviceAddress bda_address = 0;
};

struct FrameTransientAllocator {
    eastl::vector<TransientPage> pages;
    size_t current_page_index = 0;
    VkDeviceSize current_offset = 0;
};

class RenderGraph {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;
    static constexpr VkDeviceSize kDefaultTransientPageSize = 16 * 1024 * 1024; // 16 MB на страницу

    explicit RenderGraph(VulkanDevice& device);
    ~RenderGraph();

    /// Устанавливает текущий индекс кадра в полете (0..kMaxFramesInFlight-1)
    void BeginFrame(uint32_t frame_index);

    /// Выделяет динамическую транзиентную память из текущего кадрового Bump Allocator
    TransientAllocation AllocateTransient(const void* data, VkDeviceSize size, VkDeviceSize alignment = 256);

    template <typename T>
    void PutCPUData(RGTag tag, const T& data) {
        cpu_bb_.Put(tag, data);
    }

    void PutCPUData(RGTag tag, const void* data, size_t size_bytes) { cpu_bb_.Put(tag, data, size_bytes); }

    template <typename PassData, typename SetupFn, typename ExecFn>
    PassData& AddPass(eastl::string_view name, SetupFn&& setup, ExecFn&& exec) {
        auto pass = std::make_unique<PassNode<PassData>>();
        pass->name = name;
        pass->execute_fn = exec;

        PassNodeBase* raw_pass = pass.get();
        passes_.push_back(std::move(pass));

        RenderGraphBuilder builder(*this, raw_pass);
        setup(builder, static_cast<PassNode<PassData>*>(raw_pass)->data);

        return static_cast<PassNode<PassData>*>(raw_pass)->data;
    }

    RGResourceHandle ImportExternalTexture(
        eastl::string_view name,
        VkImage image,
        VkImageView image_view,
        const RGTextureDesc& desc,
        VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED
    );

    RGResourceHandle ImportExternalBuffer(
        eastl::string_view name,
        VkBuffer buffer,
        const RGBufferDesc& desc
    );

    RGResourceHandle CreateTexture(PassNodeBase* pass, eastl::string_view dbg_name, const RGTextureDesc& desc);
    RGResourceHandle CreateBuffer(PassNodeBase* pass, eastl::string_view dbg_name, const RGBufferDesc& desc);

    RGResourceHandle RegisterRead(PassNodeBase* pass, RGResourceHandle h, RGUsageHint usage);
    RGResourceHandle RegisterWrite(PassNodeBase* pass, RGResourceHandle h, RGUsageHint usage);

    void ExportTag(RGTag tag, RGResourceHandle h);
    RGResourceHandle ImportTag(PassNodeBase* pass, RGTag tag);

    void Compile();
    void Execute(VkCommandBuffer cmd);
    void Reset();

    Blackboard& GetBlackboard() { return frame_bb_; }
    const Blackboard& GetBlackboard() const { return frame_bb_; }

    CPUBlackboard& GetCPUBlackboard() { return cpu_bb_; }
    const CPUBlackboard& GetCPUBlackboard() const { return cpu_bb_; }

    VkImage GetPhysicalImage(RGResourceHandle h) const;
    VkImageView GetPhysicalImageView(RGResourceHandle h) const;
    VkBuffer GetPhysicalBuffer(RGResourceHandle h) const;

    VulkanDevice& GetDevice() const { return device_; }

private:
    void CullPasses();
    void TopologicalSort();
    void BuildBarriers();
    void CleanupTransientAllocators();

    VulkanDevice& device_;
    eastl::vector<std::unique_ptr<PassNodeBase>> passes_;
    eastl::vector<RGVirtualResource> resources_;
    Blackboard frame_bb_;
    CPUBlackboard cpu_bb_;
    RGResourcePool pool_;

    eastl::fixed_vector<uint32_t, 128> sorted_order_;

    // Аллокаторы для кадровой динамической памяти
    uint32_t frame_index_ = 0;
    FrameTransientAllocator transient_allocators_[kMaxFramesInFlight];
};

}  // namespace tryengine::graphics