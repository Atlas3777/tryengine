#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include "engine/graphics/VulkanDevice.hpp"

#include "engine/core/Assert.hpp"
#include "engine/core/Log.hpp"

namespace tryengine::graphics {

VulkanDevice::VulkanDevice(const uint32_t width, const uint32_t height, const eastl::string_view title) {
    TRY_CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS), "SDL_Init failed: {}", SDL_GetError());

    VkResult volk_res = volkInitialize();
    TRY_CHECK(volk_res == VK_SUCCESS, "Не удалось инициализировать Volk");

    constexpr SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_VULKAN;
    window_ = SDL_CreateWindow(title.data(), static_cast<int>(width), static_cast<int>(height), flags);
    TRY_CHECK(window_, "Не удалось создать окно SDL");

    uint32_t sdl_ext_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);

    vkb::InstanceBuilder instance_builder;
    instance_builder.set_app_name("tryengine").require_api_version(1, 4, 0).use_default_debug_messenger();

#ifndef NDEBUG
    instance_builder.enable_validation_layers();
#endif

    for (uint32_t i = 0; i < sdl_ext_count; ++i)
        instance_builder.enable_extension(sdl_extensions[i]);

    auto instance_ret = instance_builder.build();
    TRY_CHECK(instance_ret.has_value(), "Ошибка создания Vulkan Instance");

    vkb::Instance vkb_instance = instance_ret.value();
    instance_ = vkb_instance.instance;
    debug_messenger_ = vkb_instance.debug_messenger;

    volkLoadInstance(instance_);

    TRY_CHECK(SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_), "Ошибка создания Surface");

    // Фичи Vulkan 1.4 (UInt8 index buffer; MeshProcessor выбирает его для мелких мешей)
    VkPhysicalDeviceVulkan14Features features14{};
    features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    features14.indexTypeUint8 = VK_TRUE;

    // Фичи Vulkan 1.3 (Dynamic Rendering и Synchronization2)
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    // Фичи Vulkan 1.2 (Buffer Device Address)
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.shaderInt8 = VK_TRUE;
    features12.shaderFloat16 = VK_TRUE;

    // Фичи Vulkan 1.1 (Shader Draw Parameters)
    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.shaderDrawParameters = VK_TRUE;

    // Фичи Vulkan 1.0
    VkPhysicalDeviceFeatures features10{};
    features10.shaderInt64 = VK_TRUE;
    features10.shaderInt16 = VK_TRUE;
    features10.multiDrawIndirect = VK_TRUE;  // <--- Включаем MultiDrawIndirect

    VkPhysicalDeviceShaderObjectFeaturesEXT shader_object_features{};
    shader_object_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
    shader_object_features.shaderObject = VK_TRUE;

    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features{};
    descriptor_buffer_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptor_buffer_features.descriptorBuffer = VK_TRUE;

    // Настройка VK_EXT_color_write_enable
    VkPhysicalDeviceColorWriteEnableFeaturesEXT color_write_features{};
    color_write_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT;
    color_write_features.colorWriteEnable = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vkb_instance};
    auto phys_ret = selector.set_surface(surface_)
                        .set_minimum_version(1, 4)
                        .set_required_features(features10)
                        .set_required_features_14(features14)
                        .set_required_features_13(features13)
                        .set_required_features_12(features12)
                        .set_required_features_11(features11)
                        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                        .add_required_extension(VK_EXT_SHADER_OBJECT_EXTENSION_NAME)
                        .add_required_extension(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)
                        .add_required_extension(VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME)
                        .add_required_extension_features(shader_object_features)
                        .add_required_extension_features(descriptor_buffer_features)
                        .add_required_extension_features(color_write_features)
                        .select();

    TRY_CHECK(phys_ret.has_value(), "Не найдено физическое устройство Vulkan 1.4. Error : {}",
              phys_ret.error().message());
    vkb::PhysicalDevice vkb_phys_device = phys_ret.value();
    physical_device_ = vkb_phys_device.physical_device;

    vkb::DeviceBuilder device_builder{vkb_phys_device};
    auto dev_ret = device_builder.build();
    TRY_CHECK(dev_ret.has_value(), "Ошибка создания Logical Device");

    vkb::Device vkb_device = dev_ret.value();
    device_ = vkb_device.device;

    volkLoadDevice(device_);

    auto gq_res = vkb_device.get_queue(vkb::QueueType::graphics);
    graphics_queue_ = gq_res.value();
    graphics_queue_family_ = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    auto pq_res = vkb_device.get_queue(vkb::QueueType::present);
    present_queue_ = pq_res.value();
    present_queue_family_ = vkb_device.get_queue_index(vkb::QueueType::present).value();

    VmaVulkanFunctions vma_vulkan_functions{};
    vma_vulkan_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vma_vulkan_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_4;
    allocator_info.physicalDevice = physical_device_;
    allocator_info.device = device_;
    allocator_info.instance = instance_;
    allocator_info.pVulkanFunctions = &vma_vulkan_functions;
    allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VkResult vma_res = vmaCreateAllocator(&allocator_info, &allocator_);
    TRY_CHECK(vma_res == VK_SUCCESS, "Не удалось создать VmaAllocator");

    CreateDefaultWhiteTexture();
}

// Заглушка для незанятых сэмплеров материала: 1x1 белая текстура, которую материал
// подставляет вместо необновлённого дескриптора. Создаётся один раз на устройство.
void VulkanDevice::CreateDefaultWhiteTexture() {
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {1, 1, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo image_alloc_info{};
    image_alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    TRY_CHECK(vmaCreateImage(allocator_, &image_info, &image_alloc_info, &default_white_texture_.image,
                             &default_white_texture_.allocation, nullptr) == VK_SUCCESS,
              "Не удалось создать текстуру-заглушку");

    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = graphics_queue_family_;

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    TRY_CHECK(vkCreateCommandPool(device_, &pool_info, nullptr, &cmd_pool) == VK_SUCCESS,
              "Не удалось создать command pool для текстуры-заглушки");

    VkCommandBufferAllocateInfo cmd_alloc_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc_info.commandPool = cmd_pool;
    cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    TRY_CHECK(vkAllocateCommandBuffers(device_, &cmd_alloc_info, &cmd) == VK_SUCCESS,
              "Не удалось выделить command buffer для текстуры-заглушки");

    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = default_white_texture_.image;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VkClearColorValue clear_color{};
    clear_color.float32[0] = 1.0f;
    clear_color.float32[1] = 1.0f;
    clear_color.float32[2] = 1.0f;
    clear_color.float32[3] = 1.0f;
    vkCmdClearColorImage(cmd, default_white_texture_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1,
                         &range);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    TRY_CHECK(vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE) == VK_SUCCESS,
              "Не удалось залить текстуру-заглушку");
    vkQueueWaitIdle(graphics_queue_);
    vkDestroyCommandPool(device_, cmd_pool, nullptr);

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = default_white_texture_.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange = range;

    TRY_CHECK(vkCreateImageView(device_, &view_info, nullptr, &default_white_texture_.view) == VK_SUCCESS,
              "Не удалось создать image view текстуры-заглушки");

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0f;

    TRY_CHECK(vkCreateSampler(device_, &sampler_info, nullptr, &default_white_texture_.sampler) == VK_SUCCESS,
              "Не удалось создать sampler текстуры-заглушки");
}

void VulkanDevice::DestroyDefaultWhiteTexture() {
    if (default_white_texture_.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, default_white_texture_.view, nullptr);
        default_white_texture_.view = VK_NULL_HANDLE;
    }
    if (default_white_texture_.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device_, default_white_texture_.sampler, nullptr);
        default_white_texture_.sampler = VK_NULL_HANDLE;
    }
    if (default_white_texture_.image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, default_white_texture_.image, default_white_texture_.allocation);
        default_white_texture_.image = VK_NULL_HANDLE;
        default_white_texture_.allocation = VK_NULL_HANDLE;
    }
}

VulkanDevice::~VulkanDevice() {
    if (device_) {
        // 1. СНАЧАЛА гарантируем, что GPU закончил ВСЕ работы
        vkDeviceWaitIdle(device_);
    }

    DestroyDefaultWhiteTexture();

    if (allocator_) {
        // 2. УДАЛЯЕМ VMA (все VmaAllocation уже должны быть освобождены!)
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }

    if (device_) {
        // 3. УНИЧТОЖАЕМ логическое устройство Vulkan
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ && instance_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (debug_messenger_ && instance_) {
        vkb::destroy_debug_utils_messenger(instance_, debug_messenger_);
        debug_messenger_ = VK_NULL_HANDLE;
    }

    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
}

}  // namespace tryengine::graphics