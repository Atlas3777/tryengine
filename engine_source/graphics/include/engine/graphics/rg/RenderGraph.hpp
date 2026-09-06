#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <SDL3/SDL_gpu.h>
#include <memory>

#include "engine/graphics/rg/RGTag.hpp"

namespace tryengine::graphics {

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
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTextureUsageFlags usage = 0;
    eastl::string debug_name;
};

struct RGBufferDesc {
    uint64_t size = 0;
    SDL_GPUBufferUsageFlags usage = 0;
    eastl::string debug_name;
};

class Blackboard {
public:
    void Put(RGTag tag, RGResourceHandle h) { entries_[tag] = h; }

    [[nodiscard]] RGResourceHandle Get(RGTag tag) const {
        auto it = entries_.find(tag);
        return it != entries_.end() ? it->second : RGResourceHandle{};
    }

    void Clear() { entries_.clear(); }

private:
    eastl::hash_map<RGTag, RGResourceHandle> entries_;
};

enum class RGAccess : uint8_t { Read, Write, Create };

struct RGResourceAccess {
    RGTag tag = 0;
    RGResourceHandle handle;
    RGAccess access = RGAccess::Read;
    SDL_GPUTextureUsageFlags usage_hint = 0;
};

class RGExecuteContext {
public:
    SDL_GPUDevice* device = nullptr;
    SDL_GPUCommandBuffer* cmd_buffer = nullptr;
    SDL_GPURenderPass* gpu_pass = nullptr;
    Blackboard* frame_bb = nullptr;

    [[nodiscard]] SDL_GPUTexture* GetTexture(RGResourceHandle h) const;
    [[nodiscard]] SDL_GPUBuffer* GetBuffer(RGResourceHandle h) const;

    void SetGraph(class RenderGraph* graph) { graph_ = graph; }

private:
    class RenderGraph* graph_ = nullptr;
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
};

template <typename PassData>
class PassNode final : public PassNodeBase {
public:
    PassData data{};
    eastl::function<void(RGExecuteContext&, const PassData&)> execute_fn; // TODO: убрать function

    void Execute(RGExecuteContext& ctx) override {
        if (execute_fn) {
            execute_fn(ctx, data);
        }
    }
};

class RenderGraph;

class RenderGraphBuilder {
public:
    explicit RenderGraphBuilder(RenderGraph& graph, PassNodeBase* pass)
        : graph_(graph), pass_(pass) {}

    RGResourceHandle CreateTexture(eastl::string_view dbg_name, const RGTextureDesc& desc);
    RGResourceHandle CreateBuffer(eastl::string_view dbg_name, const RGBufferDesc& desc);

    RGResourceHandle Read(RGResourceHandle h, SDL_GPUTextureUsageFlags hint = 0);
    RGResourceHandle Write(RGResourceHandle h, SDL_GPUTextureUsageFlags hint = 0);

    void Export(RGTag tag, RGResourceHandle h);
    RGResourceHandle Import(RGTag tag);

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

    SDL_GPUTexture* physical_texture = nullptr;
    SDL_GPUBuffer* physical_buffer = nullptr;

    PassNodeBase* producer_pass = nullptr;
    uint32_t ref_count = 0;
    bool externally_exported = false;
};

class RGResourcePool {
public:
    explicit RGResourcePool(SDL_GPUDevice* device) : device_(device) {}
    ~RGResourcePool();

    SDL_GPUTexture* AcquireTexture(const RGTextureDesc& desc);
    SDL_GPUBuffer* AcquireBuffer(const RGBufferDesc& desc);

    void ReleaseAll();

private:
    struct TextureEntry {
        SDL_GPUTexture* texture = nullptr;
        RGTextureDesc desc;
        bool in_use = false;
    };

    struct BufferEntry {
        SDL_GPUBuffer* buffer = nullptr;
        RGBufferDesc desc;
        bool in_use = false;
    };

    SDL_GPUDevice* device_ = nullptr;
    eastl::vector<TextureEntry> texture_pool_;
    eastl::vector<BufferEntry> buffer_pool_;
};

class RenderGraph {
public:
    explicit RenderGraph(SDL_GPUDevice* device);
    ~RenderGraph();

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

    RGResourceHandle ImportExternalTexture(eastl::string_view name, SDL_GPUTexture* texture, const RGTextureDesc& desc);
    RGResourceHandle ImportExternalBuffer(eastl::string_view name, SDL_GPUBuffer* buffer, const RGBufferDesc& desc);

    RGResourceHandle CreateTexture(PassNodeBase* pass, eastl::string_view dbg_name, const RGTextureDesc& desc);
    RGResourceHandle CreateBuffer(PassNodeBase* pass, eastl::string_view dbg_name, const RGBufferDesc& desc);

    RGResourceHandle RegisterRead(PassNodeBase* pass, RGResourceHandle h, SDL_GPUTextureUsageFlags hint);
    RGResourceHandle RegisterWrite(PassNodeBase* pass, RGResourceHandle h, SDL_GPUTextureUsageFlags hint);

    void ExportTag(RGTag tag, RGResourceHandle h);
    RGResourceHandle ImportTag(PassNodeBase* pass, RGTag tag);

    void Compile();
    void Execute(SDL_GPUCommandBuffer* cmd);
    void Reset();

    Blackboard& GetBlackboard() { return frame_bb_; }
    const Blackboard& GetBlackboard() const { return frame_bb_; }

    SDL_GPUTexture* GetPhysicalTexture(RGResourceHandle h) const;
    SDL_GPUBuffer* GetPhysicalBuffer(RGResourceHandle h) const;

private:
    void CullPasses();
    void TopologicalSort();

    SDL_GPUDevice* device_ = nullptr;
    eastl::vector<std::unique_ptr<PassNodeBase>> passes_;
    eastl::vector<RGVirtualResource> resources_;
    Blackboard frame_bb_;
    RGResourcePool pool_;

    eastl::fixed_vector<uint32_t, 128> sorted_order_;
};

}  // namespace tryengine::graphics