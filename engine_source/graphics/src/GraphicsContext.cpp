#include "engine/graphics/GraphicsContext.hpp"

#include <cassert>

#include "engine/core/Assert.hpp"

#include <vulkan/vulkan.h> // Обязательно для макроса VK_MAKE_API_VERSION

namespace tryengine::graphics {


GraphicsContext::GraphicsContext(const uint32_t width, const uint32_t height, const eastl::string_view title) {
    if (!TRY_VERIFY(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS), "SDL_Init failed: {}", SDL_GetError())) {
        return;
    }

    LogInfo(TRY_VARS(width, height));

    constexpr SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow(title.data(), static_cast<int>(width), static_cast<int>(height), flags);
    TRY_CHECK(window_, "Не удалось создать окно SDL: {}", SDL_GetError());

    // 1. Настраиваем Vulkan 1.2 для поддержки SPIR-V 1.5
    SDL_GPUVulkanOptions vk_options{};
    vk_options.vulkan_api_version = VK_MAKE_API_VERSION(0, 1, 2, 0);

    // 2. Создаем свойства для GPU устройства
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true); // Эквивалент true во 2-м аргументе
    SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &vk_options);

    // 3. Создаем устройство и очищаем контейнер свойств
    device_ = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);

    // device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);

    TRY_CHECK(device_, "Не удалось создать GPU Device: {}", SDL_GetError());

    TRY_CHECK(SDL_ClaimWindowForGPUDevice(device_, window_), "Не удалось привязать окно к GPU Device: {}",
              SDL_GetError());
}

GraphicsContext::~GraphicsContext() {
    if (device_) {
        if (window_) {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
        }
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
    }

    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
}
}  // namespace tryengine::graphics
