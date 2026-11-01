#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/graphics/VulkanDevice.hpp"

#include <algorithm>
#include <EASTL/algorithm.h>

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

void* RGExecuteContext::GetBufferMappedPtr(RGResourceHandle h) const {
    return graph_ ? graph_->GetBufferMappedPtr(h) : nullptr;
}

TransientAllocation RGExecuteContext::AllocateTransient(const void* data, VkDeviceSize size, VkDeviceSize alignment) const {
    return graph_ ? graph_->AllocateTransient(data, size, alignment) : TransientAllocation{};
}

const RGTextureDesc& RGExecuteContext::GetTextureDesc(RGResourceHandle h) const{
    return graph_->GetTextureDesc(h);
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

// ==================== RenderGraphBuilder ====================

RGResourceHandle RenderGraphBuilder::CreateTexture(eastl::string_view dbg_name, const RGTextureDesc& desc) {
    return graph_.CreateTexture(pass_, dbg_name, desc);
}

RGResourceHandle RenderGraphBuilder::CreateBuffer(eastl::string_view dbg_name, const RGBufferDesc& desc) {
    return graph_.CreateBuffer(pass_, dbg_name, desc);
}

RGResourceHandle RenderGraphBuilder::Read(RGResourceHandle h, RGUsageHint usage) {
    return graph_.RegisterRead(pass_, h, usage);
}

RGResourceHandle RenderGraphBuilder::Read(RGTag tag, RGUsageHint usage) {
    RGResourceHandle h = graph_.ImportTag(pass_, tag);
    return graph_.RegisterRead(pass_, h, usage);
}

RGResourceHandle RenderGraphBuilder::Write(RGResourceHandle h, RGUsageHint usage) {
    return graph_.RegisterWrite(pass_, h, usage);
}

RGResourceHandle RenderGraphBuilder::Write(RGTag tag, RGUsageHint usage) {
    RGResourceHandle h = graph_.ImportTag(pass_, tag);
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

void RenderGraphBuilder::MarkSideEffect() {
    graph_.slots_[pass_].has_side_effect = true;
}

// ==================== RGResourcePool ====================

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

    VmaAllocationInfo alloc_result{};
    VkResult res = vmaCreateBuffer(
        device_.GetAllocator(),
        &buffer_info,
        &alloc_info,
        &new_entry.buffer,
        &new_entry.allocation,
        &alloc_result
    );
    TRY_CHECK(res == VK_SUCCESS, "Ошибка создания VkBuffer в RGResourcePool: {}", desc.debug_name.c_str());

    new_entry.mapped_ptr = alloc_result.pMappedData;
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

// ==================== RenderGraph ====================

RenderGraph::RenderGraph(VulkanDevice& device) : device_(device), pool_(device) {}

RenderGraph::~RenderGraph() {
    device_.WaitIdle();
    slots_.clear();   // пассы освобождают свои GPU-ресурсы, пока устройство живо
    CleanupTransientAllocators();
}

uint32_t RenderGraph::ResolveHandleIndex(uint32_t index) const {
    while (index < handle_redirects_.size() && handle_redirects_[index] != RGResourceHandle::kInvalid) {
        index = handle_redirects_[index];
    }
    return index;
}

RGResourceHandle RenderGraph::ResolveHandle(RGResourceHandle h) const {
    if (!h.IsValid()) return h;
    return RGResourceHandle{ResolveHandleIndex(h.index), h.version};
}

[[nodiscard]] void* RenderGraph::GetBufferMappedPtr(RGResourceHandle h) const {
    if (!h.IsValid()) return nullptr;
    uint32_t idx = ResolveHandleIndex(h.index);
    if (idx >= resources_.size()) return nullptr;
    return resources_[idx].mapped_ptr;
}

const RGTextureDesc& RenderGraph::GetTextureDesc(RGResourceHandle h) const {
    static const RGTextureDesc kEmptyDesc{};
    if (!h.IsValid())
        return kEmptyDesc;
    uint32_t idx = ResolveHandleIndex(h.index);
    if (idx >= resources_.size())
        return kEmptyDesc;
    const auto& res = resources_[idx];
    if (res.type == RGResourceType::Texture || res.type == RGResourceType::ExternalTexture) {
        return res.texture_desc;
    }
    return kEmptyDesc;
}

void RenderGraph::BeginFrame(uint32_t frame_index) {
    frame_index_ = frame_index % kMaxFramesInFlight;

    // Сбрасываем только покадровое. Структура графа (пассы, ресурсы, барьеры) остаётся.
    auto& allocator = transient_allocators_[frame_index_];
    allocator.current_page_index = 0;
    allocator.current_offset = 0;

    cpu_bb_.Clear();
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
        } else if (allocator.pages[allocator.current_page_index].size < size) {
            VmaAllocator vma_alloc = device_.GetAllocator();
            if (allocator.pages[allocator.current_page_index].buffer) {
                vmaDestroyBuffer(vma_alloc, allocator.pages[allocator.current_page_index].buffer, allocator.pages[allocator.current_page_index].allocation);
            }
            allocator.pages[allocator.current_page_index] = CreatePage(size);
        }
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

void RenderGraph::ImportExternalTexture(
    RGTag tag,
    VkImage image,
    VkImageView image_view,
    const RGTextureDesc& desc,
    VkImageLayout initial_layout
) {
    TRY_ASSERT(desc.width > 0 && desc.height > 0, "Texture size must be > 0");

    const bool is_new = externals_.find(tag) == externals_.end();
    RGExternalResource& ext = externals_[tag];

    // Меняется только то, что влияет на структуру/барьеры. VkImage/VkImageView меняются каждый кадр - это не повод пересобирать.
    const bool changed = is_new ||
                         ext.type != RGResourceType::ExternalTexture ||
                         ext.texture_desc.width != desc.width ||
                         ext.texture_desc.height != desc.height ||
                         ext.texture_desc.layers != desc.layers ||
                         ext.texture_desc.mips != desc.mips ||
                         ext.texture_desc.format != desc.format ||
                         ext.texture_desc.aspect_mask != desc.aspect_mask ||
                         ext.initial_layout != initial_layout;

    ext.type = RGResourceType::ExternalTexture;
    ext.texture_desc = desc;
    ext.image = image;
    ext.view = image_view;
    ext.initial_layout = initial_layout;

    if (changed) dirty_ = true;
}

void RenderGraph::ImportExternalBuffer(
    RGTag tag,
    VkBuffer buffer,
    const RGBufferDesc& desc,
    void* mapped_ptr
) {
    const bool is_new = externals_.find(tag) == externals_.end();
    RGExternalResource& ext = externals_[tag];

    const bool changed = is_new ||
                         ext.type != RGResourceType::ExternalBuffer ||
                         ext.buffer_desc.size != desc.size ||
                         ext.buffer_desc.usage != desc.usage;

    ext.type = RGResourceType::ExternalBuffer;
    ext.buffer_desc = desc;
    ext.buffer = buffer;
    ext.mapped_ptr = mapped_ptr;

    if (changed) dirty_ = true;
}

// Переносит актуальные физические объекты из реестра в собранный граф.
void RenderGraph::BindExternals() {
    for (auto& kv : externals_) {
        RGExternalResource& ext = kv.second;
        if (ext.resource >= resources_.size()) continue;

        RGVirtualResource& res = resources_[ext.resource];
        if (ext.type == RGResourceType::ExternalTexture) {
            res.physical_image = ext.image;
            res.physical_image_view = ext.view;
        } else {
            res.physical_buffer = ext.buffer;
            res.mapped_ptr = ext.mapped_ptr;
        }
    }
}

RGResourceHandle RenderGraph::CreateTexture(uint32_t pass, eastl::string_view dbg_name, const RGTextureDesc& desc) {
    RGTag tag = RGTagOf(dbg_name);
    uint32_t target_idx = RGResourceHandle::kInvalid;

    auto it = tag_to_handle_.find(tag);
    if (it != tag_to_handle_.end()) {
        target_idx = ResolveHandleIndex(it->second.index);
        auto& res = resources_[target_idx];
        TRY_ASSERT(!res.is_defined, "Повторное CreateTexture с именем '{}' — ресурс уже определён другим пассом", dbg_name);
        res.type = RGResourceType::Texture;
        res.texture_desc = desc;
        res.producer = pass;
        res.is_defined = true;
    } else {
        RGVirtualResource res;
        res.name = dbg_name;
        res.type = RGResourceType::Texture;
        res.texture_desc = desc;
        res.producer = pass;
        res.is_defined = true;

        target_idx = static_cast<uint32_t>(resources_.size());
        resources_.push_back(std::move(res));
        handle_redirects_.push_back(RGResourceHandle::kInvalid);

        tag_to_handle_[tag] = RGResourceHandle{target_idx, 0};
    }

    RGResourceHandle handle{target_idx, 0};

    RGUsageHint initial_hint = RGUsageHint::ColorAttachment;
    if ((desc.aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0 || (desc.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        initial_hint = RGUsageHint::DepthStencilAttachment;

    slots_[pass].accesses.push_back({tag, handle, RGAccessType::Create, initial_hint});
    return handle;
}

RGResourceHandle RenderGraph::CreateBuffer(uint32_t pass, eastl::string_view dbg_name, const RGBufferDesc& desc) {
    RGTag tag = RGTagOf(dbg_name);
    uint32_t target_idx = RGResourceHandle::kInvalid;

    auto it = tag_to_handle_.find(tag);
    if (it != tag_to_handle_.end()) {
        target_idx = ResolveHandleIndex(it->second.index);
        auto& res = resources_[target_idx];
        TRY_ASSERT(!res.is_defined, "Повторное CreateBuffer с именем '{}' — ресурс уже определён другим пассом", dbg_name);
        res.type = RGResourceType::Buffer;
        res.buffer_desc = desc;
        res.producer = pass;
        res.is_defined = true;
    } else {
        RGVirtualResource res;
        res.name = dbg_name;
        res.type = RGResourceType::Buffer;
        res.buffer_desc = desc;
        res.producer = pass;
        res.is_defined = true;

        target_idx = static_cast<uint32_t>(resources_.size());
        resources_.push_back(std::move(res));
        handle_redirects_.push_back(RGResourceHandle::kInvalid);

        tag_to_handle_[tag] = RGResourceHandle{target_idx, 0};
    }

    RGResourceHandle handle{target_idx, 0};
    slots_[pass].accesses.push_back({tag, handle, RGAccessType::Create, RGUsageHint::StorageWrite});
    return handle;
}

RGResourceHandle RenderGraph::RegisterRead(uint32_t pass, RGResourceHandle h, RGUsageHint usage) {
    if (!h.IsValid()) return h;
    h = ResolveHandle(h);
    slots_[pass].accesses.push_back({0, h, RGAccessType::Read, usage});
    return h;
}

RGResourceHandle RenderGraph::RegisterWrite(uint32_t pass, RGResourceHandle h, RGUsageHint usage) {
    if (!h.IsValid()) return h;
    h = ResolveHandle(h);
    h.version++;
    resources_[h.index].producer = pass;
    slots_[pass].accesses.push_back({0, h, RGAccessType::Write, usage});
    return h;
}

void RenderGraph::ExportTag(RGTag tag, RGResourceHandle h) {
    if (!h.IsValid()) return;
    h = ResolveHandle(h);

    frame_bb_.Put(tag, h);
    resources_[h.index].externally_exported = true;

    auto it = tag_to_handle_.find(tag);
    if (it != tag_to_handle_.end()) {
        uint32_t old_idx = ResolveHandleIndex(it->second.index);
        if (old_idx != h.index) {
            handle_redirects_[old_idx] = h.index;
        }
    }

    tag_to_handle_[tag] = h;
}

RGResourceHandle RenderGraph::ImportTag(uint32_t pass, RGTag tag) {
    auto it = tag_to_handle_.find(tag);
    if (it != tag_to_handle_.end()) {
        return ResolveHandle(it->second);
    }

    // Если ресурса еще нет — создаем отложенную заглушку (Placeholder)
    RGVirtualResource res;
    res.name = GetRGTagName(tag);
    res.is_defined = false;

    uint32_t idx = static_cast<uint32_t>(resources_.size());
    resources_.push_back(std::move(res));
    handle_redirects_.push_back(RGResourceHandle::kInvalid);

    RGResourceHandle placeholder_handle{idx, 0};
    tag_to_handle_[tag] = placeholder_handle;
    return placeholder_handle;
}

// ==================== Пассы и компиляция ====================

void RenderGraph::DestroyPass(uint32_t type_id) {
    vkDeviceWaitIdle(device_.GetDevice());

    for (uint32_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].type_id == type_id) {
            slots_.erase(slots_.begin() + i);
            break;
        }
    }
    by_type_[type_id] = nullptr;
    dirty_ = true;
}

void RenderGraph::RefreshActive() {
    for (auto& slot : slots_) {
        const bool active = slot.pass->IsActive();
        if (active != slot.active) {
            slot.active = active;
            dirty_ = true;
        }
    }
}

void RenderGraph::Collect(core::Engine& engine) {
    RefreshActive();

    CollectContext ctx{engine, *this, cpu_bb_, frame_index_};
    // Индексный цикл: Collect не должен добавлять/удалять пассы, но так хотя бы нет висячих ссылок.
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].active) slots_[i].pass->Collect(ctx);
    }
}

void RenderGraph::ResolveTagBindings() {
    // 1. Раскрываем редиректы Export (Read мог попасть на заглушку ДО Export), проверяем
    //    определённость и пересобираем продюсеров: последний писатель в порядке Setup.
    for (auto& res : resources_) res.producer = kRGNoPass;

    for (uint32_t p = 0; p < slots_.size(); ++p) {
        auto& slot = slots_[p];
        for (auto& acc : slot.accesses) {
            if (!acc.handle.IsValid()) continue;

            acc.handle.index = ResolveHandleIndex(acc.handle.index);
            auto& res = resources_[acc.handle.index];
            TRY_ASSERT(res.is_defined, "Пасс '{}' использует неопределённый ресурс '{}'", slot.pass->Name(), res.name);

            if (acc.access != RGAccessType::Read) res.producer = p;
        }
    }

    // 2. Зависимости: читатель зависит от продюсера ресурса (как и раньше).
    for (uint32_t p = 0; p < slots_.size(); ++p) {
        auto& slot = slots_[p];
        for (const auto& acc : slot.accesses) {
            if (acc.access != RGAccessType::Read || !acc.handle.IsValid()) continue;

            const uint32_t producer = resources_[acc.handle.index].producer;
            if (producer != kRGNoPass && producer != p &&
                eastl::find(slot.deps.begin(), slot.deps.end(), producer) == slot.deps.end()) {
                slot.deps.push_back(producer);
            }
        }
    }
}

void RenderGraph::Compile() {
    RefreshActive();
    if (dirty_) {
        Rebuild();
        LogInfo("Render Graph Rebuid");
    }

    BindExternals();
}

void RenderGraph::Rebuild() {
    pool_.ReleaseAll();
    resources_.clear();
    tag_to_handle_.clear();
    handle_redirects_.clear();
    frame_bb_.Clear();
    sorted_order_.clear();

    for (auto& slot : slots_) {
        slot.accesses.clear();
        slot.deps.clear();
        slot.image_barriers.clear();
        slot.buffer_barriers.clear();
        slot.has_side_effect = false;
        slot.culled = false;
    }

    // 1. Внешние ресурсы из реестра - до Setup, чтобы Read/Write(tag) находили их как определённые.
    for (auto& kv : externals_) {
        const RGTag tag = kv.first;
        RGExternalResource& ext = kv.second;

        RGVirtualResource res;
        res.name = GetRGTagName(tag);
        res.type = ext.type;
        res.externally_exported = true;
        res.is_defined = true;

        if (ext.type == RGResourceType::ExternalTexture) {
            res.texture_desc = ext.texture_desc;
            res.physical_image = ext.image;
            res.physical_image_view = ext.view;
            res.current_layout = ext.initial_layout;
        } else {
            res.buffer_desc = ext.buffer_desc;
            res.physical_buffer = ext.buffer;
            res.mapped_ptr = ext.mapped_ptr;
        }

        const uint32_t idx = static_cast<uint32_t>(resources_.size());
        resources_.push_back(std::move(res));
        handle_redirects_.push_back(RGResourceHandle::kInvalid);

        const RGResourceHandle handle{idx, 0};
        tag_to_handle_[tag] = handle;
        frame_bb_.Put(tag, handle);
        ext.resource = idx;
    }

    // 2. Setup активных пассов
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].active) continue;
        RenderGraphBuilder builder(*this, i);
        slots_[i].pass->Setup(builder);
    }

    ResolveTagBindings();
    CullPasses();
    TopologicalSort();
    AllocatePhysical();
    BuildBarriers();

    dirty_ = false;
}

void RenderGraph::AllocatePhysical() {
    for (auto& res : resources_) {
        if (!res.is_defined) continue;

        if (res.type == RGResourceType::Texture) {
            auto pooled = pool_.AcquireTexture(res.texture_desc);
            res.physical_image = pooled.image;
            res.physical_image_view = pooled.view;
            res.image_allocation = pooled.allocation;
        } else if (res.type == RGResourceType::Buffer) {
            auto pooled = pool_.AcquireBuffer(res.buffer_desc);
            res.physical_buffer = pooled.buffer;
            res.buffer_allocation = pooled.allocation;
            res.mapped_ptr = pooled.mapped_ptr;
        }
    }
}

void RenderGraph::CullPasses() {
    for (auto& res : resources_) res.ref_count = 0;
    for (auto& slot : slots_) {
        slot.culled = !slot.active;   // неактивные пассы в граф не попадают
        for (auto& acc : slot.accesses) {
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
        const uint32_t res_idx = zero_ref_stack.back();
        zero_ref_stack.pop_back();

        const uint32_t producer_idx = resources_[res_idx].producer;
        if (producer_idx == kRGNoPass) continue;

        PassSlot& producer = slots_[producer_idx];
        // culled: пасс с двумя мёртвыми выходами иначе вырезался бы дважды (двойной декремент ref_count)
        if (producer.has_side_effect || producer.culled) continue;

        bool any_output_alive = false;
        for (const auto& a : producer.accesses) {
            if (a.access != RGAccessType::Read && a.handle.IsValid() && resources_[a.handle.index].ref_count > 0) {
                any_output_alive = true;
                break;
            }
        }
        if (any_output_alive) continue;

        producer.culled = true;
        for (auto& a : producer.accesses) {
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

    const uint32_t n = static_cast<uint32_t>(slots_.size());
    eastl::vector<uint32_t> in_degree(n, 0u);
    eastl::vector<uint8_t> done(n, uint8_t(0));

    uint32_t alive = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (slots_[i].culled) continue;
        ++alive;
        for (uint32_t dep : slots_[i].deps) {
            if (!slots_[dep].culled) in_degree[i]++;
        }
    }

    // Kahn: из готовых берём пасс с наименьшим индексом - независимые пассы идут в порядке регистрации.
    for (uint32_t emitted = 0; emitted < alive; ++emitted) {
        uint32_t u = n;
        for (uint32_t i = 0; i < n; ++i) {
            if (!slots_[i].culled && !done[i] && in_degree[i] == 0) {
                u = i;
                break;
            }
        }
        TRY_ASSERT(u != n, "RenderGraph: циклическая зависимость между пассами");

        done[u] = 1;
        sorted_order_.push_back(u);

        for (uint32_t v = 0; v < n; ++v) {
            if (slots_[v].culled || done[v]) continue;
            for (uint32_t dep : slots_[v].deps) {
                if (dep == u) in_degree[v]--;
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
        auto& pass = slots_[pass_idx];
        if (pass.culled) continue;

        pass.image_barriers.clear();
        pass.buffer_barriers.clear();

        for (const auto& acc : pass.accesses) {
            if (!acc.handle.IsValid()) continue;
            uint32_t real_idx = ResolveHandleIndex(acc.handle.index);
            if (real_idx >= resources_.size()) continue;

            auto& res = resources_[real_idx];
            if (!res.is_defined) continue;

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

                    pass.image_barriers.push_back(RGImageBarrier{barrier, real_idx});

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

                    pass.buffer_barriers.push_back(RGBufferBarrier{barrier, real_idx});

                    res.current_stage = target.stage;
                    res.current_access = target.access;
                }
            }
        }
    }
}

void RenderGraph::Execute(VkCommandBuffer cmd) {
    TRY_ASSERT(!dirty_, "RenderGraph::Execute вызван без Compile");

    RGExecuteContext ctx;
    ctx.device = device_.GetDevice();
    ctx.cmd_buffer = cmd;
    ctx.frame_bb = &frame_bb_;
    ctx.cpu_bb = &cpu_bb_;
    ctx.SetGraph(this);

    eastl::fixed_vector<VkImageMemoryBarrier2, 16> image_barriers;
    eastl::fixed_vector<VkBufferMemoryBarrier2, 8> buffer_barriers;

    for (uint32_t pass_idx : sorted_order_) {
        auto& slot = slots_[pass_idx];

        // Барьеры закэшированы вместе с индексом ресурса; физический объект подставляем сейчас.
        image_barriers.clear();
        buffer_barriers.clear();
        for (const auto& rec : slot.image_barriers) {
            image_barriers.push_back(rec.barrier);
            image_barriers.back().image = resources_[rec.resource].physical_image;
        }
        for (const auto& rec : slot.buffer_barriers) {
            buffer_barriers.push_back(rec.barrier);
            buffer_barriers.back().buffer = resources_[rec.resource].physical_buffer;
        }

        if (!image_barriers.empty() || !buffer_barriers.empty()) {
            VkDependencyInfo dep_info{};
            dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep_info.imageMemoryBarrierCount = static_cast<uint32_t>(image_barriers.size());
            dep_info.pImageMemoryBarriers = image_barriers.data();
            dep_info.bufferMemoryBarrierCount = static_cast<uint32_t>(buffer_barriers.size());
            dep_info.pBufferMemoryBarriers = buffer_barriers.data();

            vkCmdPipelineBarrier2(cmd, &dep_info);
        }

        slot.pass->Execute(ctx);
    }
}

VkImage RenderGraph::GetPhysicalImage(RGResourceHandle h) const {
    if (!h.IsValid()) return VK_NULL_HANDLE;
    uint32_t idx = ResolveHandleIndex(h.index);
    if (idx >= resources_.size()) return VK_NULL_HANDLE;
    return resources_[idx].physical_image;
}

VkImageView RenderGraph::GetPhysicalImageView(RGResourceHandle h) const {
    if (!h.IsValid()) return VK_NULL_HANDLE;
    uint32_t idx = ResolveHandleIndex(h.index);
    if (idx >= resources_.size()) return VK_NULL_HANDLE;
    return resources_[idx].physical_image_view;
}

VkBuffer RenderGraph::GetPhysicalBuffer(RGResourceHandle h) const {
    if (!h.IsValid()) return VK_NULL_HANDLE;
    uint32_t idx = ResolveHandleIndex(h.index);
    if (idx >= resources_.size()) return VK_NULL_HANDLE;
    return resources_[idx].physical_buffer;
}

}  // namespace tryengine::graphics