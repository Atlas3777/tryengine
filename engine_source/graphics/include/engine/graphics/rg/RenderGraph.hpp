#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <SDL3/SDL_gpu.h>
#include <cstring>
#include <memory>
#include <utility>

#include "engine/core/Log.hpp"
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
        if (it != entries_.end())
            return it->second;

        LogError("Resource not found ");
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

        LogError("CPUBlackboard: Entry not found for tag.");
        return eastl::nullopt;
    }

    [[nodiscard]] const void* GetData(RGTag tag) const {
        auto span = GetSpan(tag);
        return span->data();
    }

    [[nodiscard]] size_t GetSize(RGTag tag) const {
        auto span = GetSpan(tag);
        return span->size();
    }

    template <typename T>
    [[nodiscard]] const T* GetAs(RGTag tag) const {
        auto span = GetSpan(tag);
        if (span->size() >= sizeof(T)) {
            return reinterpret_cast<const T*>(span->data());
        }
        LogError("CPUBlackboard: Data size is smaller than requested struct.");
        return nullptr;
    }

    [[nodiscard]] bool Has(RGTag tag) const { return entries_.find(tag) != entries_.end(); }

    void Clear() { entries_.clear(); }

private:
    eastl::hash_map<RGTag, eastl::vector<uint8_t>> entries_;
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
    CPUBlackboard* cpu_bb = nullptr;

    [[nodiscard]] SDL_GPUTexture* GetTexture(RGResourceHandle h) const;
    [[nodiscard]] SDL_GPUBuffer* GetBuffer(RGResourceHandle h) const;

    void SetGraph(class RenderGraph* graph) { graph_ = graph; }

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

class RenderGraph;

class RenderGraphBuilder {
public:
    explicit RenderGraphBuilder(RenderGraph& graph, PassNodeBase* pass) : graph_(graph), pass_(pass) {}

    RGResourceHandle CreateTexture(eastl::string_view dbg_name, const RGTextureDesc& desc);
    RGResourceHandle CreateBuffer(eastl::string_view dbg_name, const RGBufferDesc& desc);

    RGResourceHandle Read(RGResourceHandle h, SDL_GPUTextureUsageFlags hint = 0);
    RGResourceHandle Write(RGResourceHandle h, SDL_GPUTextureUsageFlags hint = 0);

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

    // Запись CPU-данных ДО создания пассов
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

    CPUBlackboard& GetCPUBlackboard() { return cpu_bb_; }
    const CPUBlackboard& GetCPUBlackboard() const { return cpu_bb_; }

    SDL_GPUTexture* GetPhysicalTexture(RGResourceHandle h) const;
    SDL_GPUBuffer* GetPhysicalBuffer(RGResourceHandle h) const;

private:
    void CullPasses();
    void TopologicalSort();

    SDL_GPUDevice* device_ = nullptr;
    eastl::vector<std::unique_ptr<PassNodeBase>> passes_;
    eastl::vector<RGVirtualResource> resources_;
    Blackboard frame_bb_;
    CPUBlackboard cpu_bb_;
    RGResourcePool pool_;

    eastl::fixed_vector<uint32_t, 128> sorted_order_;
};

}  // namespace tryengine::graphics