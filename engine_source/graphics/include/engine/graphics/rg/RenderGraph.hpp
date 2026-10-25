#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <EASTL/fixed_vector.h>
#include <EASTL/functional.h>
#include <EASTL/hash_map.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>
#include <vk_mem_alloc.h>
#include <volk.h>

#include "engine/core/Assert.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/TypeRegistry.hpp"
#include "engine/graphics/rg/IRenderPass.hpp"
#include "engine/graphics/rg/RGTag.hpp"

namespace tryengine::core {
class Engine;
}

namespace tryengine::graphics {

class VulkanDevice;

struct RGResourceHandle {
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;
    uint32_t index = kInvalid;
    uint16_t version = 0;

    [[nodiscard]] constexpr bool IsValid() const { return index != kInvalid; }
    friend bool operator==(const RGResourceHandle&, const RGResourceHandle&) = default;
};

enum class RGResourceType : uint8_t { Texture, Buffer, ExternalTexture, ExternalBuffer };

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

enum class RGAccessType : uint8_t { Read, Write, Create };

enum class RGUsageHint : uint32_t {
    None = 0,
    ColorAttachment = 1 << 0,
    DepthStencilAttachment = 1 << 1,
    DepthStencilReadOnly = 1 << 2,
    ShaderRead = 1 << 3,
    StorageRead = 1 << 4,
    StorageWrite = 1 << 5,
    StorageReadWrite = StorageRead | StorageWrite,
    TransferSrc = 1 << 6,
    TransferDst = 1 << 7,
    Present = 1 << 8
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

struct TransientAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceAddress bda_address = 0;
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

        return RGResourceHandle{};
    }

    [[nodiscard]] bool Has(RGTag tag) const { return entries_.find(tag) != entries_.end(); }

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

        LogError("CPUBlackboard: Запись для тега {} не найдена.", GetRGTagName(tag));
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
    [[nodiscard]] void* GetBufferMappedPtr(RGResourceHandle h) const;

    void BeginRendering(eastl::span<const RGColorAttachment> color_attachments,
                        const RGDepthStencilAttachment* depth_attachment = nullptr, VkRect2D render_area = {},
                        uint32_t layer_count = 1);

    void EndRendering();

    [[nodiscard]] TransientAllocation AllocateTransient(const void* data, VkDeviceSize size,
                                                        VkDeviceSize alignment = 256) const;

    void SetGraph(RenderGraph* graph) { graph_ = graph; }

private:
    RenderGraph* graph_ = nullptr;
};

// Контекст сбора данных: передаётся в IRenderPass::Collect каждый кадр.
struct CollectContext {
    core::Engine& engine;
    RenderGraph& graph;  // чтобы пасс мог сам импортировать свои внешние ресурсы
    CPUBlackboard& cpu_bb;
    uint32_t frame_index;
};

inline constexpr uint32_t kRGNoPass = 0xFFFFFFFFu;

// Барьер хранится вместе с индексом ресурса: физический VkImage/VkBuffer подставляется
// в Execute (swapchain-image и readback-буфер меняются каждый кадр, а граф закэширован).
struct RGImageBarrier {
    VkImageMemoryBarrier2 barrier{};
    uint32_t resource = 0;
};

struct RGBufferBarrier {
    VkBufferMemoryBarrier2 barrier{};
    uint32_t resource = 0;
};

// Служебное состояние пасса внутри графа (раньше лежало в PassNodeBase).
struct PassSlot {
    std::unique_ptr<IRenderPass> pass;
    uint32_t type_id = 0;
    bool active = true;
    bool culled = false;
    bool has_side_effect = false;

    eastl::fixed_vector<RGResourceAccess, 16> accesses;
    eastl::fixed_vector<uint32_t, 8> deps;

    eastl::vector<RGImageBarrier> image_barriers;
    eastl::vector<RGBufferBarrier> buffer_barriers;
};

class RenderGraphBuilder {
public:
    RenderGraphBuilder(RenderGraph& graph, uint32_t pass) : graph_(graph), pass_(pass) {}

    RGResourceHandle CreateTexture(eastl::string_view dbg_name, const RGTextureDesc& desc);
    RGResourceHandle CreateBuffer(eastl::string_view dbg_name, const RGBufferDesc& desc);

    RGResourceHandle Read(RGResourceHandle h, RGUsageHint usage = RGUsageHint::ShaderRead);
    RGResourceHandle Read(RGTag tag, RGUsageHint usage = RGUsageHint::ShaderRead);

    RGResourceHandle Write(RGResourceHandle h, RGUsageHint usage = RGUsageHint::StorageWrite);
    RGResourceHandle Write(RGTag tag, RGUsageHint usage = RGUsageHint::StorageWrite);

    void Export(RGTag tag, RGResourceHandle h);
    RGResourceHandle Import(RGTag tag);

    CPUBlackboard& GetCPUBlackboard();
    const CPUBlackboard& GetCPUBlackboard() const;

    void MarkSideEffect();

private:
    RenderGraph& graph_;
    uint32_t pass_;  // индекс слота
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
    void* mapped_ptr = nullptr;

    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 current_stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 current_access = VK_ACCESS_2_NONE;

    uint32_t producer = kRGNoPass;  // было: PassNodeBase* producer_pass
    uint32_t ref_count = 0;
    bool externally_exported = false;
    bool is_defined = false;
};

// Реестр внешних ресурсов: переживает пересборку графа, физические объекты обновляются каждый кадр.
struct RGExternalResource {
    RGResourceType type = RGResourceType::ExternalTexture;
    RGTextureDesc texture_desc;
    RGBufferDesc buffer_desc;

    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkBuffer buffer = VK_NULL_HANDLE;
    void* mapped_ptr = nullptr;

    uint32_t resource = RGResourceHandle::kInvalid;  // индекс в resources_ после последней сборки
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
        void* mapped_ptr = nullptr;
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

class
RenderGraph {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;
    static constexpr VkDeviceSize kDefaultTransientPageSize = 16 * 1024 * 1024;

    explicit RenderGraph(VulkanDevice& device);
    ~RenderGraph();

    // ---------- Пассы (НОВОЕ) ----------
    template <typename T, typename... Args>
    T& AddPass(Args&&... args) {
        static_assert(std::is_base_of_v<IRenderPass, T>, "T должен наследоваться от IRenderPass");

        const uint32_t id = core::ScopedTypeId<RenderGraph, T>::Value();
        if (id >= by_type_.size())
            by_type_.resize(id + 1, nullptr);
        TRY_ASSERT(by_type_[id] == nullptr, "AddPass: пасс этого типа уже добавлен");

        auto& slot = slots_.emplace_back();
        slot.pass = std::make_unique<T>(std::forward<Args>(args)...);
        slot.type_id = id;
        by_type_[id] = slot.pass.get();

        dirty_ = true;
        return static_cast<T&>(*slot.pass);
    }

    template <typename T>
    [[nodiscard]] T* Get() const {
        const uint32_t id = core::ScopedTypeId<RenderGraph, T>::Value();
        return id < by_type_.size() ? static_cast<T*>(by_type_[id]) : nullptr;
    }

    // Ждёт GPU (удаление редкое), уничтожает пасс и помечает граф на пересборку.
    template <typename T>
    void RemovePass() {
        const uint32_t id = core::ScopedTypeId<RenderGraph, T>::Value();
        if (id < by_type_.size() && by_type_[id])
            DestroyPass(id);
    }

    // Resize вьюпорта, смена настроек графики и т.п.
    void Invalidate() { dirty_ = true; }

    // ---------- Кадр: BeginFrame -> ImportExternal* -> Collect -> Compile -> Execute ----------
    void BeginFrame(uint32_t frame_index);
    void Collect(core::Engine& engine);
    void Compile();  // no-op, если структура не менялась
    void Execute(VkCommandBuffer cmd);

    // Вызывать каждый кадр (сам или из IRenderPass::Collect). Смена desc => пересборка.
    void ImportExternalTexture(RGTag tag, VkImage image, VkImageView image_view, const RGTextureDesc& desc,
                               VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED);
    void ImportExternalBuffer(RGTag tag, VkBuffer buffer, const RGBufferDesc& desc, void* mapped_ptr = nullptr);

    TransientAllocation AllocateTransient(const void* data, VkDeviceSize size, VkDeviceSize alignment = 256);

    template <typename T>
    void PutCPUData(RGTag tag, const T& data) {
        cpu_bb_.Put(tag, data);
    }

    void PutCPUData(RGTag tag, const void* data, size_t size_bytes) { cpu_bb_.Put(tag, data, size_bytes); }

    Blackboard& GetBlackboard() { return frame_bb_; }
    const Blackboard& GetBlackboard() const { return frame_bb_; }

    CPUBlackboard& GetCPUBlackboard() { return cpu_bb_; }
    const CPUBlackboard& GetCPUBlackboard() const { return cpu_bb_; }

    VkImage GetPhysicalImage(RGResourceHandle h) const;
    VkImageView GetPhysicalImageView(RGResourceHandle h) const;
    VkBuffer GetPhysicalBuffer(RGResourceHandle h) const;

    [[nodiscard]] uint32_t ResolveHandleIndex(uint32_t index) const;
    [[nodiscard]] RGResourceHandle ResolveHandle(RGResourceHandle h) const;

    [[nodiscard]] void* GetBufferMappedPtr(RGResourceHandle h) const;

    VulkanDevice& GetDevice() const { return device_; }
    bool IsDirty() const {return dirty_;};

private:
    friend class RenderGraphBuilder;

    // API билдера (pass - индекс слота)
    RGResourceHandle CreateTexture(uint32_t pass, eastl::string_view dbg_name, const RGTextureDesc& desc);
    RGResourceHandle CreateBuffer(uint32_t pass, eastl::string_view dbg_name, const RGBufferDesc& desc);
    RGResourceHandle RegisterRead(uint32_t pass, RGResourceHandle h, RGUsageHint usage);
    RGResourceHandle RegisterWrite(uint32_t pass, RGResourceHandle h, RGUsageHint usage);
    void ExportTag(RGTag tag, RGResourceHandle h);
    RGResourceHandle ImportTag(uint32_t pass, RGTag tag);

    void DestroyPass(uint32_t type_id);
    void RefreshActive();
    void Rebuild();
    void BindExternals();
    void ResolveTagBindings();
    void CullPasses();
    void TopologicalSort();
    void AllocatePhysical();
    void BuildBarriers();
    void CleanupTransientAllocators();

    VulkanDevice& device_;

    // Пассы: владение внутри графа
    eastl::vector<PassSlot> slots_;
    eastl::vector<IRenderPass*> by_type_;  // индекс = ScopedTypeId<RenderGraph, T>
    bool dirty_ = true;

    // Результат сборки (живёт, пока dirty_ == false)
    eastl::vector<RGVirtualResource> resources_;
    eastl::hash_map<RGTag, RGResourceHandle> tag_to_handle_;
    eastl::vector<uint32_t> handle_redirects_;
    eastl::fixed_vector<uint32_t, 128> sorted_order_;
    Blackboard frame_bb_;
    RGResourcePool pool_;

    // Внешние ресурсы переживают пересборку
    eastl::hash_map<RGTag, RGExternalResource> externals_;

    // Покадровое
    CPUBlackboard cpu_bb_;
    uint32_t frame_index_ = 0;
    FrameTransientAllocator transient_allocators_[kMaxFramesInFlight];
};

}  // namespace tryengine::graphics