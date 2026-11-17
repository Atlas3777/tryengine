#pragma once

#include <cstdint>

namespace tryengine::graphics {

//! Сколько кадров CPU готовит вперёд, пока GPU ещё выполняет предыдущие.
//!
//! Одно значение на весь движок. От него зависят:
//!   - FrameSync          — command pool/buffer, image_available semaphore, fence;
//!   - RenderGraph        — транзиентные аллокаторы (по одному слоту на кадр в полёте);
//!   - PickingPass        — слоты readback'а пикселя;
//!   - GpuProfiler        — слоты запросов таймстемпов (читаются через N кадров).
//!
//! Значение НЕ равно числу изображений swapchain: картинок может быть 3, а кадров в
//! полёте — 2. Это разные сущности с разным временем жизни (см. AGENTS.md graphics).
inline constexpr uint32_t kMaxFramesInFlight = 2;

}  // namespace tryengine::graphics
