#pragma once

#include <volk.h>

#include "engine/core/Profiler.hpp"
#include "engine/graphics/FrameConfig.hpp"

#if TRY_PROFILE_ENABLED

#include "engine/graphics/VulkanDevice.hpp"

namespace tryengine::graphics {

/// Сколько пассов за кадр максимум замеряется: 2 таймстемпа на пасс.
inline constexpr uint32_t kMaxProfiledPasses = 64;
inline constexpr uint32_t kQueriesPerFrame = kMaxProfiledPasses * 2;

//! GPU-таймеры на VkQueryPool с VK_QUERY_TYPE_TIMESTAMP.
//!
//! Замеряет каждый пасс рендер-графа: RenderGraph пишет таймстемп до и после
//! IRenderPass::Execute, поэтому пассы инструментировать не нужно.
//!
//! Результаты доступны не сразу: таймстемпы читаются, когда GPU закончил кадр, то есть
//! через kMaxFramesInFlight кадров, ровно в тот момент, когда FrameSync дождался fence
//! этого слота и слот можно переиспользовать. Поэтому BeginFrame сначала читает прошлые
//! значения, а потом сбрасывает запросы.
//!
//! Если у семейства очередей `timestampValidBits == 0`, класс неактивен: WriteTimestamp
//! и чтение не выполняются, панель просто не покажет GPU-строки.
class GpuProfiler {
public:
    explicit GpuProfiler(VulkanDevice& device);
    ~GpuProfiler();

    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    [[nodiscard]] bool IsSupported() const {
        return supported_;
    }

    /// Читает результаты прошлого использования слота. Вызывать в начале кадра, после того
    /// как fence этого слота дождались.
    void BeginFrame(uint32_t frame_index);

    /// Сбрасывает запросы слота командой в command buffer. Host-функция vkResetQueryPool
    /// требует фичи hostQueryReset, а vkCmdResetQueryPool — нет, поэтому сброс идёт по GPU.
    void BeginCommands(VkCommandBuffer cmd);

    void BeginPass(VkCommandBuffer cmd, const char* name);
    void EndPass(VkCommandBuffer cmd);

    /// Фиксирует, что в слоте есть результаты для чтения в следующем цикле.
    void EndFrame(uint32_t frame_index);

private:
    void WriteTimestamp(VkCommandBuffer cmd, uint32_t query_in_slot);
    void ReadPreviousResults(uint32_t slot);

    VulkanDevice& device_;
    VkQueryPool query_pool_ = VK_NULL_HANDLE;
    bool supported_ = false;
    float ms_per_tick_ = 0.0f;
    uint64_t valid_mask_ = ~0ull;

    uint32_t current_slot_ = 0;
    uint32_t pass_count_ = 0;
    bool pending_reset_ = false;
    uint32_t slot_passes_[kMaxFramesInFlight] = {};
    uint32_t slot_frame_[kMaxFramesInFlight] = {};
    const char* slot_names_[kMaxFramesInFlight][kMaxProfiledPasses] = {};
};

}  // namespace tryengine::graphics

#else  // TRY_PROFILE_ENABLED

// Профилирование вырезано: GPU-таймеров нет, методы — пустышки, чтобы RenderGraph
// не оброс #ifdef'ами.
namespace tryengine::graphics {

class VulkanDevice;

class GpuProfiler {
public:
    explicit GpuProfiler(VulkanDevice&) {}

    [[nodiscard]] static bool IsSupported() {
        return false;
    }

    void BeginFrame(uint32_t) {}

    void BeginPass(VkCommandBuffer, const char*) {}

    void EndPass(VkCommandBuffer) {}

    void EndFrame(uint32_t) {}
};

}  // namespace tryengine::graphics

#endif  // TRY_PROFILE_ENABLED
