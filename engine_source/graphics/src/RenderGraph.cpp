#include "engine/graphics/rg/RenderGraph.hpp"

#include <algorithm>

namespace tryengine::graphics {

SDL_GPUTexture* RGExecuteContext::GetTexture(RGResourceHandle h) const {
    return graph_ ? graph_->GetPhysicalTexture(h) : nullptr;
}

SDL_GPUBuffer* RGExecuteContext::GetBuffer(RGResourceHandle h) const {
    return graph_ ? graph_->GetPhysicalBuffer(h) : nullptr;
}

RGResourceHandle RenderGraphBuilder::CreateTexture(eastl::string_view dbg_name, const RGTextureDesc& desc) {
    return graph_.CreateTexture(pass_, dbg_name, desc);
}

RGResourceHandle RenderGraphBuilder::CreateBuffer(eastl::string_view dbg_name, const RGBufferDesc& desc) {
    return graph_.CreateBuffer(pass_, dbg_name, desc);
}

RGResourceHandle RenderGraphBuilder::Read(RGResourceHandle h, SDL_GPUTextureUsageFlags hint) {
    return graph_.RegisterRead(pass_, h, hint);
}

RGResourceHandle RenderGraphBuilder::Write(RGResourceHandle h, SDL_GPUTextureUsageFlags hint) {
    return graph_.RegisterWrite(pass_, h, hint);
}

void RenderGraphBuilder::Export(RGTag tag, RGResourceHandle h) {
    graph_.ExportTag(tag, h);
}

RGResourceHandle RenderGraphBuilder::Import(RGTag tag) {
    return graph_.ImportTag(pass_, tag);
}

RGResourcePool::~RGResourcePool() {
    for (auto& entry : texture_pool_) {
        if (entry.texture) SDL_ReleaseGPUTexture(device_, entry.texture);
    }
    for (auto& entry : buffer_pool_) {
        if (entry.buffer) SDL_ReleaseGPUBuffer(device_, entry.buffer);
    }
}

SDL_GPUTexture* RGResourcePool::AcquireTexture(const RGTextureDesc& desc) {
    for (auto& entry : texture_pool_) {
        if (!entry.in_use && entry.desc.width == desc.width && entry.desc.height == desc.height &&
            entry.desc.format == desc.format && entry.desc.usage == desc.usage) {
            entry.in_use = true;
            return entry.texture;
        }
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = desc.format;
    info.usage = desc.usage;
    info.width = desc.width;
    info.height = desc.height;
    info.layer_count_or_depth = desc.layers;
    info.num_levels = desc.mips;

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device_, &info);
    texture_pool_.push_back({tex, desc, true});
    return tex;
}

SDL_GPUBuffer* RGResourcePool::AcquireBuffer(const RGBufferDesc& desc) {
    for (auto& entry : buffer_pool_) {
        if (!entry.in_use && entry.desc.size >= desc.size && entry.desc.usage == desc.usage) {
            entry.in_use = true;
            return entry.buffer;
        }
    }

    SDL_GPUBufferCreateInfo info{};
    info.usage = desc.usage;
    info.size = static_cast<uint32_t>(desc.size);

    SDL_GPUBuffer* buf = SDL_CreateGPUBuffer(device_, &info);
    buffer_pool_.push_back({buf, desc, true});
    return buf;
}

void RGResourcePool::ReleaseAll() {
    for (auto& entry : texture_pool_) entry.in_use = false;
    for (auto& entry : buffer_pool_) entry.in_use = false;
}

RenderGraph::RenderGraph(SDL_GPUDevice* device) : device_(device), pool_(device) {}

RenderGraph::~RenderGraph() = default;

RGResourceHandle RenderGraph::ImportExternalTexture(eastl::string_view name, SDL_GPUTexture* texture, const RGTextureDesc& desc) {
    RGVirtualResource res;
    res.name = name;
    res.type = RGResourceType::ExternalTexture;
    res.texture_desc = desc;
    res.physical_texture = texture;
    res.externally_exported = true;

    uint32_t idx = static_cast<uint32_t>(resources_.size());
    resources_.push_back(std::move(res));
    return RGResourceHandle{idx, 0};
}

RGResourceHandle RenderGraph::ImportExternalBuffer(eastl::string_view name, SDL_GPUBuffer* buffer, const RGBufferDesc& desc) {
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
    pass->accesses.push_back({0, handle, RGAccess::Create, desc.usage});
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
    pass->accesses.push_back({0, handle, RGAccess::Create, 0});
    return handle;
}

RGResourceHandle RenderGraph::RegisterRead(PassNodeBase* pass, RGResourceHandle h, SDL_GPUTextureUsageFlags hint) {
    if (!h.IsValid()) return h;
    pass->accesses.push_back({0, h, RGAccess::Read, hint});
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

RGResourceHandle RenderGraph::RegisterWrite(PassNodeBase* pass, RGResourceHandle h, SDL_GPUTextureUsageFlags hint) {
    if (!h.IsValid()) return h;
    h.version++;
    pass->accesses.push_back({0, h, RGAccess::Write, hint});
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
        RegisterRead(pass, h, 0);
    }
    return h;
}

void RenderGraph::Compile() {
    CullPasses();
    TopologicalSort();

    pool_.ReleaseAll();
    for (auto& res : resources_) {
        if (res.type == RGResourceType::Texture) {
            res.physical_texture = pool_.AcquireTexture(res.texture_desc);
        } else if (res.type == RGResourceType::Buffer) {
            res.physical_buffer = pool_.AcquireBuffer(res.buffer_desc);
        }
    }
}

void RenderGraph::CullPasses() {
    for (auto& res : resources_) res.ref_count = 0;
    for (auto& pass : passes_) {
        pass->culled = false;
        for (auto& acc : pass->accesses) {
            if (acc.access == RGAccess::Read && acc.handle.IsValid()) {
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
            if (a.access != RGAccess::Read && a.handle.IsValid() && resources_[a.handle.index].ref_count > 0) {
                any_output_alive = true;
                break;
            }
        }
        if (any_output_alive) continue;

        producer->culled = true;
        for (auto& a : producer->accesses) {
            if (a.access == RGAccess::Read && a.handle.IsValid()) {
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

void RenderGraph::Execute(SDL_GPUCommandBuffer* cmd) {
    RGExecuteContext ctx;
    ctx.device = device_;
    ctx.cmd_buffer = cmd;
    ctx.frame_bb = &frame_bb_;
    ctx.SetGraph(this);

    for (uint32_t idx : sorted_order_) {
        if (!passes_[idx]->culled) {
            passes_[idx]->Execute(ctx);
        }
    }
}

void RenderGraph::Reset() {
    passes_.clear();
    resources_.clear();
    frame_bb_.Clear();
    sorted_order_.clear();
}

SDL_GPUTexture* RenderGraph::GetPhysicalTexture(RGResourceHandle h) const {
    if (!h.IsValid() || h.index >= resources_.size()) return nullptr;
    return resources_[h.index].physical_texture;
}

SDL_GPUBuffer* RenderGraph::GetPhysicalBuffer(RGResourceHandle h) const {
    if (!h.IsValid() || h.index >= resources_.size()) return nullptr;
    return resources_[h.index].physical_buffer;
}

}  // namespace tryengine::graphics