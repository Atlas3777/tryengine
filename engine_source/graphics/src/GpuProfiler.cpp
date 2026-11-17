#include "engine/graphics/GpuProfiler.hpp"

#if TRY_PROFILE_ENABLED

#include <EASTL/vector.h>

#include "engine/core/Log.hpp"

namespace tryengine::graphics {

GpuProfiler::GpuProfiler(VulkanDevice& device) : device_(device) {
    const VkPhysicalDevice physical_device = device_.GetPhysicalDevice();

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
    if (device_.GetGraphicsQueueFamily() >= family_count) {
        LogWarn("GpuProfiler: неизвестное семейство очередей, GPU-таймеры отключены");
        return;
    }

    eastl::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());

    const uint32_t valid_bits = families[device_.GetGraphicsQueueFamily()].timestampValidBits;
    if (valid_bits == 0) {
        LogInfo("GpuProfiler: очередь не поддерживает timestamp, GPU-таймеры отключены");
        return;
    }

    // Таймстемп не обязан занимать все 64 бита: лишние старшие биты маскируются.
    valid_mask_ = valid_bits >= 64 ? ~0ull : ((1ull << valid_bits) - 1ull);
    ms_per_tick_ = static_cast<float>(properties.limits.timestampPeriod) / 1'000'000.0f;

    VkQueryPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    pool_info.queryCount = kMaxFramesInFlight * kQueriesPerFrame;

    const VkResult result = vkCreateQueryPool(device_.GetDevice(), &pool_info, nullptr, &query_pool_);
    if (result != VK_SUCCESS) {
        LogError("GpuProfiler: не удалось создать VkQueryPool, код {}", static_cast<int>(result));
        return;
    }

    supported_ = true;
}

GpuProfiler::~GpuProfiler() {
    if (query_pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device_.GetDevice(), query_pool_, nullptr);
        query_pool_ = VK_NULL_HANDLE;
    }
}

void GpuProfiler::BeginFrame(const uint32_t frame_index) {
    if (!supported_)
        return;

    current_slot_ = frame_index % kMaxFramesInFlight;
    pass_count_ = 0;

    // Слот переиспользуется: fence этого кадра только что дождались, результаты прошлого
    // цикла готовы. Сами запросы сбрасывает BeginCommands — командой в command buffer,
    // потому что vkResetQueryPool требует включённой фичи hostQueryReset.
    ReadPreviousResults(current_slot_);
    slot_passes_[current_slot_] = 0;
    pending_reset_ = true;
}

void GpuProfiler::BeginCommands(const VkCommandBuffer cmd) {
    if (!supported_ || !pending_reset_)
        return;
    vkCmdResetQueryPool(cmd, query_pool_, current_slot_ * kQueriesPerFrame, kQueriesPerFrame);
    pending_reset_ = false;
}

void GpuProfiler::BeginPass(const VkCommandBuffer cmd, const char* name) {
    if (!supported_ || pass_count_ >= kMaxProfiledPasses)
        return;
    slot_names_[current_slot_][pass_count_] = name;
    WriteTimestamp(cmd, pass_count_ * 2);
}

void GpuProfiler::EndPass(const VkCommandBuffer cmd) {
    if (!supported_ || pass_count_ >= kMaxProfiledPasses)
        return;
    WriteTimestamp(cmd, pass_count_ * 2 + 1);
    ++pass_count_;
}

void GpuProfiler::EndFrame(const uint32_t frame_index) {
    if (!supported_)
        return;
    // Читать будем через kMaxFramesInFlight кадров, когда GPU гарантированно закончил этот.
    // Номер кадра запоминаем сейчас: к моменту чтения профилировщик закроет ещё несколько
    // кадров, и события должны попасть в свой frame, а не в текущий.
    const uint32_t slot = frame_index % kMaxFramesInFlight;
    slot_passes_[slot] = pass_count_;
    slot_frame_[slot] = core::Profiler::Instance().FrameCount();
}

void GpuProfiler::WriteTimestamp(const VkCommandBuffer cmd, const uint32_t query_in_slot) {
    const uint32_t query = current_slot_ * kQueriesPerFrame + query_in_slot;
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, query_pool_, query);
}

void GpuProfiler::ReadPreviousResults(const uint32_t slot) {
    const uint32_t passes = slot_passes_[slot];
    if (passes == 0)
        return;

    uint64_t timestamps[kMaxProfiledPasses * 2] = {};
    const uint32_t count = passes * 2;
    const VkResult result =
        vkGetQueryPoolResults(device_.GetDevice(), query_pool_, slot * kQueriesPerFrame, count, sizeof(timestamps),
                              timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS) {
        // VK_NOT_READY здесь означал бы, что fence слота не дождались; кадр пропускаем.
        return;
    }

    // Первый таймстемп кадра — начало шкалы таймлайна для GPU-пассов.
    const uint64_t frame_begin = timestamps[0] & valid_mask_;
    for (uint32_t i = 0; i < passes; ++i) {
        const uint64_t begin = timestamps[i * 2] & valid_mask_;
        const uint64_t end = timestamps[i * 2 + 1] & valid_mask_;
        // Вычитание беззнаковое: переполнение счётчика таймстемпа не даёт отрицательного времени.
        const float start_ms = static_cast<float>(begin - frame_begin) * ms_per_tick_;
        const float duration_ms = static_cast<float>(end - begin) * ms_per_tick_;
        core::Profiler::Instance().AppendGpuEvent(slot_frame_[slot], slot_names_[slot][i], start_ms, duration_ms);
    }
}

}  // namespace tryengine::graphics

#endif  // TRY_PROFILE_ENABLED
