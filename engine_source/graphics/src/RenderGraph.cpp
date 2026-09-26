#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/graphics/VulkanDevice.hpp"

#include <algorithm>

namespace tryengine::graphics {

VkImage RGExecuteContext::GetTexture(RGResourceHandle h) const {
    return graph_ ? graph_->GetPhysicalImage(h) : VK_NULL_HANDLE;
}

VkImageView RGExecuteContext::GetImageView(RGResourceHandle h) const {
    return graph_ ? graph_->GetPhysicalImageView(h) : VK_NULL_HANDLE;
}

VkBuffer RGExecuteContext::GetBuffer(RGResourceHandle h) const {
    return graph_ ? graph_->GetPhysicalBuffer(h) : VK_NULL_HANDLE;
}

TransientAllocation RGExecuteContext::AllocateTransient(const void* data, VkDeviceSize size, VkDeviceSize alignment) const {
    return graph_ ? graph_->AllocateTransient(data, size, alignment) : TransientAllocation{};
}

void RGExecuteContext::BeginRendering(
    eastl::span<const RGColorAttachment> color_attachments,
    const RGDepthStencilAttachment* depth_attachment,
    VkRect2D render_area,
    uint32_t layer_count
) {
    eastl::fixed_vector<VkRenderingAttachmentInfo, 8> color_attachment_infos;

    for (const auto& ca : color_attachments) {
        VkImageView view = GetImageView(ca.handle);
        VkRenderingAttachmentInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageView = view;
        info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        info.resolveMode = (ca.resolve_view != VK_NULL_HANDLE) ? VK_RESOLVE_MODE_AVERAGE_BIT : VK_RESOLVE_MODE_NONE;
        info.resolveImageView = ca.resolve_view;
        info.resolveImageLayout = (ca.resolve_view != VK_NULL_HANDLE) ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        info.loadOp = ca.load_op;
        info.storeOp = ca.store_op;
        info.clearValue = ca.clear_value;

        color_attachment_infos.push_back(info);
    }

    VkRenderingAttachmentInfo depth_info{};
    if (depth_attachment) {
        VkImageView view = GetImageView(depth_attachment->handle);
        depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_info.imageView = view;
        depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_info.loadOp = depth_attachment->depth_load_op;
        depth_info.storeOp = depth_attachment->depth_store_op;
        depth_info.clearValue = depth_attachment->clear_value;
    }

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea = render_area;
    rendering_info.layerCount = layer_count;
    rendering_info.colorAttachmentCount = static_cast<uint32_t>(color_attachment_infos.size());
    rendering_info.pColorAttachments = color_attachment_infos.data();
    rendering_info.pDepthAttachment = depth_attachment ? &depth_info : nullptr;
    rendering_info.pStencilAttachment = nullptr;

    vkCmdBeginRendering(cmd_buffer, &rendering_info);
}

void RGExecuteContext::EndRendering() {
    vkCmdEndRendering(cmd_buffer);
}

RGResourceHandle RenderGraphBuilder::CreateTexture(eastl::string_view dbg_name, const RGTextureDesc& desc) {
    return graph_.CreateTexture(pass_, dbg_name, desc);
}

RGResourceHandle RenderGraphBuilder::CreateBuffer(eastl::string_view dbg_name, const RGBufferDesc& desc) {
    return graph_.CreateBuffer(pass_, dbg_name, desc);
}

RGResourceHandle RenderGraphBuilder::Read(RGResourceHandle h, RGUsageHint usage) {
    return graph_.RegisterRead(pass_, h, usage);
}

RGResourceHandle RenderGraphBuilder::Write(RGResourceHandle h, RGUsageHint usage) {
    return graph_.RegisterWrite(pass_, h, usage);
}

void RenderGraphBuilder::Export(RGTag tag, RGResourceHandle h) {
    graph_.ExportTag(tag, h);
}

RGResourceHandle RenderGraphBuilder::Import(RGTag tag) {
    return graph_.ImportTag(pass_, tag);
}

CPUBlackboard& RenderGraphBuilder::GetCPUBlackboard() {
    return graph_.GetCPUBlackboard();
}

const CPUBlackboard& RenderGraphBuilder::GetCPUBlackboard() const {
    return graph_.GetCPUBlackboard();
}

RGResourcePool::RGResourcePool(VulkanDevice& device) : device_(device) {}

RGResourcePool::~RGResourcePool() {
    DestroyAll();
}

RGResourcePool::PooledTexture RGResourcePool::AcquireTexture(const RGTextureDesc& desc) {
    for (auto& entry : texture_pool_) {
        if (!entry.in_use &&
            entry.desc.width == desc.width &&
            entry.desc.height == desc.height &&
            entry.desc.format == desc.format &&
            entry.desc.layers == desc.layers &&
            entry.desc.mips == desc.mips &&
            entry.desc.usage == desc.usage) {
            entry.in_use = true;
            return entry;
        }
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = desc.format;
    image_info.extent = {desc.width, desc.height, 1};
    image_info.mipLevels = desc.mips;
    image_info.arrayLayers = desc.layers;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = desc.usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    PooledTexture new_entry{};
    new_entry.desc = desc;

    VkResult res = vmaCreateImage(
        device_.GetAllocator(),
        &image_info,
        &alloc_info,
        &new_entry.image,
        &new_entry.allocation,
        nullptr
    );
    TRY_CHECK(res == VK_SUCCESS, "Ошибка выделения памяти для VkImage в RGResourcePool: {}", desc.debug_name.c_str());

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = new_entry.image;
    view_info.viewType = (desc.layers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = desc.format;
    view_info.subresourceRange.aspectMask = desc.aspect_mask;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = desc.mips;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = desc.layers;

    res = vkCreateImageView(device_.GetDevice(), &view_info, nullptr, &new_entry.view);
    TRY_CHECK(res == VK_SUCCESS, "Ошибка создания VkImageView в RGResourcePool: {}", desc.debug_name.c_str());

    new_entry.in_use = true;
    texture_pool_.push_back(new_entry);
    return new_entry;
}

RGResourcePool::PooledBuffer RGResourcePool::AcquireBuffer(const RGBufferDesc& desc) {
    for (auto& entry : buffer_pool_) {
        if (!entry.in_use && entry.desc.size >= desc.size && entry.desc.usage == desc.usage) {
            entry.in_use = true;
            return entry;
        }
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = desc.size;
    buffer_info.usage = desc.usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = desc.memory_usage;
    alloc_info.flags = desc.alloc_flags;

    PooledBuffer new_entry{};
    new_entry.desc = desc;

    VkResult res = vmaCreateBuffer(
        device_.GetAllocator(),
        &buffer_info,
        &alloc_info,
        &new_entry.buffer,
        &new_entry.allocation,
        nullptr
    );
    TRY_CHECK(res == VK_SUCCESS, "Ошибка создания VkBuffer в RGResourcePool: {}", desc.debug_name.c_str());

    new_entry.in_use = true;
    buffer_pool_.push_back(new_entry);
    return new_entry;
}

void RGResourcePool::ReleaseAll() {
    for (auto& entry : texture_pool_) entry.in_use = false;
    for (auto& entry : buffer_pool_) entry.in_use = false;
}

void RGResourcePool::DestroyAll() {
    VkDevice dev = device_.GetDevice();
    VmaAllocator alloc = device_.GetAllocator();

    for (auto& entry : texture_pool_) {
        if (entry.view) vkDestroyImageView(dev, entry.view, nullptr);
        if (entry.image) vmaDestroyImage(alloc, entry.image, entry.allocation);
    }
    texture_pool_.clear();

    for (auto& entry : buffer_pool_) {
        if (entry.buffer) vmaDestroyBuffer(alloc, entry.buffer, entry.allocation);
    }
    buffer_pool_.clear();
}

RenderGraph::RenderGraph(VulkanDevice& device) : device_(device), pool_(device) {}

RenderGraph::~RenderGraph() {
    CleanupTransientAllocators();
}

void RenderGraph::BeginFrame(uint32_t frame_index) {
    frame_index_ = frame_index % kMaxFramesInFlight;
    Reset();
}

TransientAllocation RenderGraph::AllocateTransient(const void* data, VkDeviceSize size, VkDeviceSize alignment) {
    if (size == 0) return TransientAllocation{};
    if (alignment == 0) alignment = 16;

    auto& allocator = transient_allocators_[frame_index_];

    auto CreatePage = [this](VkDeviceSize min_size) -> TransientPage {
        VkDeviceSize page_size = std::max(kDefaultTransientPageSize, min_size);

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = page_size;
        buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;

        TransientPage page{};
        page.size = page_size;

        VmaAllocationInfo result_info{};
        VkResult res = vmaCreateBuffer(
            device_.GetAllocator(),
            &buffer_info,
            &alloc_info,
            &page.buffer,
            &page.allocation,
            &result_info
        );
        TRY_CHECK(res == VK_SUCCESS, "Ошибка создания страницы транзиентного буфера в RenderGraph");

        page.mapped_ptr = static_cast<uint8_t*>(result_info.pMappedData);

        VkBufferDeviceAddressInfo bda_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = page.buffer
        };
        page.bda_address = vkGetBufferDeviceAddress(device_.GetDevice(), &bda_info);

        return page;
    };

    if (allocator.pages.empty()) {
        allocator.pages.push_back(CreatePage(size));
        allocator.current_page_index = 0;
        allocator.current_offset = 0;
    }

    TransientPage* page = &allocator.pages[allocator.current_page_index];
    VkDeviceSize aligned_offset = (allocator.current_offset + alignment - 1) & ~(alignment - 1);

    if (aligned_offset + size > page->size) {
        allocator.current_page_index++;
        if (allocator.current_page_index >= allocator.pages.size()) {
            allocator.pages.push_back(CreatePage(size));
        }
        allocator.current_page_index = allocator.pages.size() - 1;
        page = &allocator.pages[allocator.current_page_index];
        aligned_offset = 0;
    }

    TransientAllocation alloc{};
    alloc.buffer = page->buffer;
    alloc.offset = aligned_offset;
    alloc.bda_address = page->bda_address ? (page->bda_address + aligned_offset) : 0;
    alloc.mapped_ptr = page->mapped_ptr + aligned_offset;

    if (data && alloc.mapped_ptr) {
        std::memcpy(alloc.mapped_ptr, data, size);
    }

    allocator.current_offset = aligned_offset + size;
    return alloc;
}

void RenderGraph::CleanupTransientAllocators() {
    VmaAllocator allocator = device_.GetAllocator();
    for (uint32_t f = 0; f < kMaxFramesInFlight; ++f) {
        for (auto& page : transient_allocators_[f].pages) {
            if (page.buffer) {
                vmaDestroyBuffer(allocator, page.buffer, page.allocation);
            }
        }
        transient_allocators_[f].pages.clear();
    }
}

RGResourceHandle RenderGraph::ImportExternalTexture(
    eastl::string_view name,
    VkImage image,
    VkImageView image_view,
    const RGTextureDesc& desc,
    VkImageLayout initial_layout
) {
    TRY_ASSERT(desc.width > 0 || desc.height > 0, "Texture need be > 0 size");
    RGVirtualResource res;
    res.name = name;
    res.type = RGResourceType::ExternalTexture;
    res.texture_desc = desc;
    res.physical_image = image;
    res.physical_image_view = image_view;
    res.current_layout = initial_layout;
    res.externally_exported = true;

    uint32_t idx = static_cast<uint32_t>(resources_.size());
    resources_.push_back(std::move(res));
    return RGResourceHandle{idx, 0};
}

RGResourceHandle RenderGraph::ImportExternalBuffer(
    eastl::string_view name,
    VkBuffer buffer,
    const RGBufferDesc& desc
) {
    RGVirtualResource res;
    res.name = name;
    res.type = RGResourceType::ExternalBuffer;
    res.buffer_desc = desc;
    res.physical_buffer = buffer;
    res.externally_exported = true;

    uint32_t idx = static_cast<uint32_t>(resources_.size());
    resources_.push_back(std::move(res));
    return RGResourceHandle{idx, 0};
}

RGResourceHandle RenderGraph::CreateTexture(PassNodeBase* pass, eastl::string_view dbg_name, const RGTextureDesc& desc) {
    RGVirtualResource res;
    res.name = dbg_name;
    res.type = RGResourceType::Texture;
    res.texture_desc = desc;
    res.producer_pass = pass;

    uint32_t idx = static_cast<uint32_t>(resources_.size());
    resources_.push_back(std::move(res));

    RGResourceHandle handle{idx, 0};

    RGUsageHint initial_hint = RGUsageHint::ColorAttachment;
    if ((desc.aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0 || (desc.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        initial_hint = RGUsageHint::DepthStencilAttachment;

    pass->accesses.push_back({0, handle, RGAccessType::Create, initial_hint});
    return handle;
}

RGResourceHandle RenderGraph::CreateBuffer(PassNodeBase* pass, eastl::string_view dbg_name, const RGBufferDesc& desc) {
    RGVirtualResource res;
    res.name = dbg_name;
    res.type = RGResourceType::Buffer;
    res.buffer_desc = desc;
    res.producer_pass = pass;

    uint32_t idx = static_cast<uint32_t>(resources_.size());
    resources_.push_back(std::move(res));

    RGResourceHandle handle{idx, 0};
    pass->accesses.push_back({0, handle, RGAccessType::Create, RGUsageHint::StorageWrite});
    return handle;
}

RGResourceHandle RenderGraph::RegisterRead(PassNodeBase* pass, RGResourceHandle h, RGUsageHint usage) {
    if (!h.IsValid()) return h;
    pass->accesses.push_back({0, h, RGAccessType::Read, usage});
    if (resources_[h.index].producer_pass) {
        uint32_t producer_idx = 0;
        for (uint32_t i = 0; i < passes_.size(); ++i) {
            if (passes_[i].get() == resources_[h.index].producer_pass) {
                producer_idx = i;
                break;
            }
        }
        pass->deps.push_back(producer_idx);
    }
    return h;
}

RGResourceHandle RenderGraph::RegisterWrite(PassNodeBase* pass, RGResourceHandle h, RGUsageHint usage) {
    if (!h.IsValid()) return h;
    h.version++;
    resources_[h.index].producer_pass = pass;
    pass->accesses.push_back({0, h, RGAccessType::Write, usage});
    return h;
}

void RenderGraph::ExportTag(RGTag tag, RGResourceHandle h) {
    if (!h.IsValid()) return;
    frame_bb_.Put(tag, h);
    resources_[h.index].externally_exported = true;
}

RGResourceHandle RenderGraph::ImportTag(PassNodeBase* pass, RGTag tag) {
    RGResourceHandle h = frame_bb_.Get(tag);
    if (h.IsValid()) {
        RegisterRead(pass, h, RGUsageHint::ShaderRead);
    }
    return h;
}

void RenderGraph::Compile() {
    CullPasses();
    TopologicalSort();

    pool_.ReleaseAll();
    for (auto& res : resources_) {
        if (res.type == RGResourceType::Texture) {
            auto pooled = pool_.AcquireTexture(res.texture_desc);
            res.physical_image = pooled.image;
            res.physical_image_view = pooled.view;
            res.image_allocation = pooled.allocation;
        } else if (res.type == RGResourceType::Buffer) {
            auto pooled = pool_.AcquireBuffer(res.buffer_desc);
            res.physical_buffer = pooled.buffer;
            res.buffer_allocation = pooled.allocation;
        }
    }

    BuildBarriers();
}

void RenderGraph::CullPasses() {
    for (auto& res : resources_) res.ref_count = 0;
    for (auto& pass : passes_) {
        pass->culled = false;
        for (auto& acc : pass->accesses) {
            if (acc.access == RGAccessType::Read && acc.handle.IsValid()) {
                resources_[acc.handle.index].ref_count++;
            }
        }
    }

    eastl::fixed_vector<uint32_t, 64> zero_ref_stack;
    for (uint32_t i = 0; i < resources_.size(); ++i) {
        if (resources_[i].ref_count == 0 && !resources_[i].externally_exported) {
            zero_ref_stack.push_back(i);
        }
    }

    while (!zero_ref_stack.empty()) {
        uint32_t res_idx = zero_ref_stack.back();
        zero_ref_stack.pop_back();

        PassNodeBase* producer = resources_[res_idx].producer_pass;
        if (!producer || producer->has_side_effect) continue;

        bool any_output_alive = false;
        for (const auto& a : producer->accesses) {
            if (a.access != RGAccessType::Read && a.handle.IsValid() && resources_[a.handle.index].ref_count > 0) {
                any_output_alive = true;
                break;
            }
        }
        if (any_output_alive) continue;

        producer->culled = true;
        for (auto& a : producer->accesses) {
            if (a.access == RGAccessType::Read && a.handle.IsValid()) {
                if (--resources_[a.handle.index].ref_count == 0) {
                    zero_ref_stack.push_back(a.handle.index);
                }
            }
        }
    }
}

void RenderGraph::TopologicalSort() {
    sorted_order_.clear();
    eastl::fixed_vector<uint32_t, 128> in_degree(passes_.size(), 0);

    for (uint32_t i = 0; i < passes_.size(); ++i) {
        if (passes_[i]->culled) continue;
        for (uint32_t dep : passes_[i]->deps) {
            if (!passes_[dep]->culled) {
                in_degree[i]++;
            }
        }
    }

    eastl::fixed_vector<uint32_t, 64> ready;
    for (uint32_t i = 0; i < passes_.size(); ++i) {
        if (!passes_[i]->culled && in_degree[i] == 0) {
            ready.push_back(i);
        }
    }

    while (!ready.empty()) {
        uint32_t u = ready.back();
        ready.pop_back();
        sorted_order_.push_back(u);

        for (uint32_t v = 0; v < passes_.size(); ++v) {
            if (passes_[v]->culled) continue;
            for (uint32_t dep : passes_[v]->deps) {
                if (dep == u) {
                    if (--in_degree[v] == 0) {
                        ready.push_back(v);
                    }
                }
            }
        }
    }
}

struct StateTransitionInfo {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
};

static StateTransitionInfo MapTextureState(RGAccessType access, RGUsageHint usage) {
    StateTransitionInfo info;

    if (usage & RGUsageHint::ColorAttachment) {
        info.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        info.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        info.access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (usage & RGUsageHint::DepthStencilAttachment) {
        info.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        info.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        info.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    } else if (usage & RGUsageHint::DepthStencilReadOnly) {
        info.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        info.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        info.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    } else if (usage & RGUsageHint::ShaderRead) {
        info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    } else if (usage & RGUsageHint::StorageReadWrite) {
        info.layout = VK_IMAGE_LAYOUT_GENERAL;
        info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    } else if (usage & RGUsageHint::StorageRead) {
        info.layout = VK_IMAGE_LAYOUT_GENERAL;
        info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    } else if (usage & RGUsageHint::StorageWrite) {
        info.layout = VK_IMAGE_LAYOUT_GENERAL;
        info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    } else if (usage & RGUsageHint::TransferSrc) {
        info.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        info.stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        info.access = VK_ACCESS_2_TRANSFER_READ_BIT;
    } else if (usage & RGUsageHint::TransferDst) {
        info.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        info.stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        info.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    } else if (usage & RGUsageHint::Present) {
        info.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        info.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        info.access = VK_ACCESS_2_NONE;
    } else {
        if (access == RGAccessType::Read) {
            info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            info.access = VK_ACCESS_2_SHADER_READ_BIT;
        } else {
            info.layout = VK_IMAGE_LAYOUT_GENERAL;
            info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            info.access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        }
    }

    return info;
}

static StateTransitionInfo MapBufferState(RGAccessType access, RGUsageHint usage) {
    StateTransitionInfo info;

    if (usage & RGUsageHint::ShaderRead) {
        info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT;
    } else if (usage & RGUsageHint::StorageReadWrite) {
        info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    } else if (usage & RGUsageHint::StorageRead) {
        info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    } else if (usage & RGUsageHint::StorageWrite) {
        info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    } else if (usage & RGUsageHint::TransferSrc) {
        info.stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        info.access = VK_ACCESS_2_TRANSFER_READ_BIT;
    } else if (usage & RGUsageHint::TransferDst) {
        info.stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        info.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    } else {
        if (access == RGAccessType::Read) {
            info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            info.access = VK_ACCESS_2_MEMORY_READ_BIT;
        } else {
            info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            info.access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        }
    }

    return info;
}

void RenderGraph::BuildBarriers() {
    for (uint32_t pass_idx : sorted_order_) {
        auto& pass = passes_[pass_idx];
        if (pass->culled) continue;

        pass->image_barriers.clear();
        pass->buffer_barriers.clear();

        for (const auto& acc : pass->accesses) {
            if (!acc.handle.IsValid() || acc.handle.index >= resources_.size()) continue;

            auto& res = resources_[acc.handle.index];

            if (res.type == RGResourceType::Texture || res.type == RGResourceType::ExternalTexture) {
                StateTransitionInfo target = MapTextureState(acc.access, acc.usage_hint);

                bool layout_mismatch = (res.current_layout != target.layout);
                constexpr VkAccessFlags2 write_flags = VK_ACCESS_2_SHADER_WRITE_BIT |
                                                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                                       VK_ACCESS_2_TRANSFER_WRITE_BIT |
                                                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

                bool is_write = (acc.access == RGAccessType::Write || acc.access == RGAccessType::Create || (target.access & write_flags) != 0);
                bool was_write = (res.current_access & write_flags) != 0;

                if (layout_mismatch || is_write || was_write) {
                    VkImageMemoryBarrier2 barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.srcStageMask = (res.current_stage == VK_PIPELINE_STAGE_2_NONE) ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : res.current_stage;
                    barrier.srcAccessMask = res.current_access;
                    barrier.dstStageMask = target.stage;
                    barrier.dstAccessMask = target.access;
                    barrier.oldLayout = res.current_layout;
                    barrier.newLayout = target.layout;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = res.physical_image;
                    barrier.subresourceRange.aspectMask = res.texture_desc.aspect_mask;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.levelCount = res.texture_desc.mips;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = res.texture_desc.layers;

                    pass->image_barriers.push_back(barrier);

                    res.current_layout = target.layout;
                    res.current_stage = target.stage;
                    res.current_access = target.access;
                }
            } else if (res.type == RGResourceType::Buffer || res.type == RGResourceType::ExternalBuffer) {
                StateTransitionInfo target = MapBufferState(acc.access, acc.usage_hint);

                constexpr VkAccessFlags2 write_flags = VK_ACCESS_2_SHADER_WRITE_BIT |
                                                       VK_ACCESS_2_TRANSFER_WRITE_BIT |
                                                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

                bool is_write = (acc.access == RGAccessType::Write || acc.access == RGAccessType::Create || (target.access & write_flags) != 0);
                bool was_write = (res.current_access & write_flags) != 0;

                if (is_write || was_write) {
                    VkBufferMemoryBarrier2 barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    barrier.srcStageMask = (res.current_stage == VK_PIPELINE_STAGE_2_NONE) ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : res.current_stage;
                    barrier.srcAccessMask = res.current_access;
                    barrier.dstStageMask = target.stage;
                    barrier.dstAccessMask = target.access;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.buffer = res.physical_buffer;
                    barrier.offset = 0;
                    barrier.size = VK_WHOLE_SIZE;

                    pass->buffer_barriers.push_back(barrier);

                    res.current_stage = target.stage;
                    res.current_access = target.access;
                }
            }
        }
    }
}

void RenderGraph::Execute(VkCommandBuffer cmd) {
    RGExecuteContext ctx;
    ctx.device = device_.GetDevice();
    ctx.cmd_buffer = cmd;
    ctx.frame_bb = &frame_bb_;
    ctx.cpu_bb = &cpu_bb_;
    ctx.SetGraph(this);

    for (uint32_t pass_idx : sorted_order_) {
        auto& pass = passes_[pass_idx];
        if (pass->culled) continue;

        if (!pass->image_barriers.empty() || !pass->buffer_barriers.empty()) {
            VkDependencyInfo dep_info{};
            dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep_info.imageMemoryBarrierCount = static_cast<uint32_t>(pass->image_barriers.size());
            dep_info.pImageMemoryBarriers = pass->image_barriers.data();
            dep_info.bufferMemoryBarrierCount = static_cast<uint32_t>(pass->buffer_barriers.size());
            dep_info.pBufferMemoryBarriers = pass->buffer_barriers.data();

            vkCmdPipelineBarrier2(cmd, &dep_info);
        }

        pass->Execute(ctx);
    }
}

void RenderGraph::Reset() {
    auto& allocator = transient_allocators_[frame_index_];
    allocator.current_page_index = 0;
    allocator.current_offset = 0;

    passes_.clear();
    resources_.clear();
    frame_bb_.Clear();
    cpu_bb_.Clear();
    sorted_order_.clear();
}

VkImage RenderGraph::GetPhysicalImage(RGResourceHandle h) const {
    if (!h.IsValid() || h.index >= resources_.size()) return VK_NULL_HANDLE;
    return resources_[h.index].physical_image;
}

VkImageView RenderGraph::GetPhysicalImageView(RGResourceHandle h) const {
    if (!h.IsValid() || h.index >= resources_.size()) return VK_NULL_HANDLE;
    return resources_[h.index].physical_image_view;
}

VkBuffer RenderGraph::GetPhysicalBuffer(RGResourceHandle h) const {
    if (!h.IsValid() || h.index >= resources_.size()) return VK_NULL_HANDLE;
    return resources_[h.index].physical_buffer;
}

}  // namespace tryengine::graphics