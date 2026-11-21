#pragma once

#include <vk_mem_alloc.h>

#include "../../../../resources/include/engine/resources/binary/MeshBinary.hpp"

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <EASTL/fixed_vector.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstring>
#include <mutex>
#include <utility>
#include <volk.h>

#include "engine/core/Assert.hpp"
#include "engine/core/Log.hpp"
#include "engine/graphics/ShaderReflection.hpp"
#include "engine/resources/ResourceHandle.hpp"
#include "engine/resources/Vertex.hpp"

namespace tryengine::graphics {

inline VkDescriptorType MapResourceKindToVkDescriptorType(ShaderResourceKind kind) {
    switch (kind) {
        case ShaderResourceKind::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case ShaderResourceKind::SampledTexture:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case ShaderResourceKind::StorageBufferRead:
        case ShaderResourceKind::StorageBufferWrite:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case ShaderResourceKind::StorageTexture:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

inline VkShaderStageFlags MapToVkShaderStageFlags(ShaderStageFlags flags) {
    VkShaderStageFlags result = 0;
    if (flags & ShaderStageFlagBits::Vertex)
        result |= VK_SHADER_STAGE_VERTEX_BIT;
    if (flags & ShaderStageFlagBits::Fragment)
        result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (flags & ShaderStageFlagBits::Compute)
        result |= VK_SHADER_STAGE_COMPUTE_BIT;
    return result != 0 ? result : VK_SHADER_STAGE_ALL_GRAPHICS;
}

struct Shader {
    VkDevice device_cache = VK_NULL_HANDLE;

    VkShaderEXT vertex_shader = VK_NULL_HANDLE;
    VkShaderEXT fragment_shader = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    eastl::vector<VkDescriptorSetLayout> descriptor_set_layouts;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkShaderStageFlags per_obj_push_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    ShaderReflectionData reflection;
    eastl::vector<uint8_t> default_uniform_data;

    Shader(VkDevice device_) {
        device_cache = device_;
    };

    // Запрет копирования
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Перемещение
    Shader(Shader&& other) noexcept {
        *this = std::move(other);
    }

    Shader& operator=(Shader&& other) noexcept {
        if (this != &other) {
            Destroy();

            device_cache = std::exchange(other.device_cache, VK_NULL_HANDLE);
            vertex_shader = std::exchange(other.vertex_shader, VK_NULL_HANDLE);
            fragment_shader = std::exchange(other.fragment_shader, VK_NULL_HANDLE);
            layout = std::exchange(other.layout, VK_NULL_HANDLE);
            descriptor_set_layouts = std::move(other.descriptor_set_layouts);
            descriptor_pool = std::exchange(other.descriptor_pool, VK_NULL_HANDLE);
            per_obj_push_stages = other.per_obj_push_stages;
            reflection = std::move(other.reflection);
            default_uniform_data = std::move(other.default_uniform_data);
        }
        return *this;
    }

    ~Shader() {
        Destroy();
    }

    void Destroy(VkDevice device) {
        device_cache = device;
        Destroy();
    }

    void Destroy() {
        if (device_cache == VK_NULL_HANDLE)
            return;

        if (vertex_shader) {
            vkDestroyShaderEXT(device_cache, vertex_shader, nullptr);
            vertex_shader = VK_NULL_HANDLE;
        }
        if (fragment_shader) {
            vkDestroyShaderEXT(device_cache, fragment_shader, nullptr);
            fragment_shader = VK_NULL_HANDLE;
        }
        if (layout) {
            vkDestroyPipelineLayout(device_cache, layout, nullptr);
            layout = VK_NULL_HANDLE;
        }
        for (auto& set_layout : descriptor_set_layouts) {
            if (set_layout) {
                vkDestroyDescriptorSetLayout(device_cache, set_layout, nullptr);
                set_layout = VK_NULL_HANDLE;
            }
        }
        descriptor_set_layouts.clear();
        if (descriptor_pool) {
            vkDestroyDescriptorPool(device_cache, descriptor_pool, nullptr);
            descriptor_pool = VK_NULL_HANDLE;
        }

        device_cache = VK_NULL_HANDLE;
    }
};

struct Mesh {
    VmaAllocator allocator_cache = VK_NULL_HANDLE;

    VkBuffer position_buffer = VK_NULL_HANDLE;
    VmaAllocation position_allocation = VK_NULL_HANDLE;
    VkDeviceAddress position_buffer_addr = 0;  // BDA

    VkBuffer attribute_buffer = VK_NULL_HANDLE;
    VmaAllocation attribute_allocation = VK_NULL_HANDLE;
    VkDeviceAddress attribute_buffer_addr = 0;  // BDA

    VkBuffer index_buffer = VK_NULL_HANDLE;
    VmaAllocation index_allocation = VK_NULL_HANDLE;
    VkDeviceAddress index_buffer_addr = 0;  // BDA

    uint32_t num_indices = 0;
    resources::MeshAttributeFlags attribute_flags = resources::MeshAttributeFlags::None;
    uint16_t attribute_stride = 0;
    VkIndexType index_type = VK_INDEX_TYPE_NONE_KHR;

    Mesh() = default;

    // Запрет копирования
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Перемещение
    Mesh(Mesh&& other) noexcept {
        *this = std::move(other);
    }

    Mesh& operator=(Mesh&& other) noexcept {
        if (this != &other) {
            Destroy();

            allocator_cache = std::exchange(other.allocator_cache, VK_NULL_HANDLE);

            position_buffer = std::exchange(other.position_buffer, VK_NULL_HANDLE);
            position_allocation = std::exchange(other.position_allocation, VK_NULL_HANDLE);
            position_buffer_addr = std::exchange(other.position_buffer_addr, 0);

            attribute_buffer = std::exchange(other.attribute_buffer, VK_NULL_HANDLE);
            attribute_allocation = std::exchange(other.attribute_allocation, VK_NULL_HANDLE);
            attribute_buffer_addr = std::exchange(other.attribute_buffer_addr, 0);

            index_buffer = std::exchange(other.index_buffer, VK_NULL_HANDLE);
            index_allocation = std::exchange(other.index_allocation, VK_NULL_HANDLE);
            index_buffer_addr = std::exchange(other.index_buffer_addr, 0);

            num_indices = std::exchange(other.num_indices, 0);
            attribute_flags = other.attribute_flags;
            attribute_stride = other.attribute_stride;
            index_type = other.index_type;
        }
        return *this;
    }

    ~Mesh() {
        Destroy();
    }

    void Destroy(VmaAllocator allocator) {
        allocator_cache = allocator;
        Destroy();
    }

    void Destroy() {
        if (allocator_cache == VK_NULL_HANDLE)
            return;

        if (position_buffer) {
            vmaDestroyBuffer(allocator_cache, position_buffer, position_allocation);
            position_buffer = VK_NULL_HANDLE;
            position_allocation = VK_NULL_HANDLE;
            position_buffer_addr = 0;
        }
        if (attribute_buffer) {
            vmaDestroyBuffer(allocator_cache, attribute_buffer, attribute_allocation);
            attribute_buffer = VK_NULL_HANDLE;
            attribute_allocation = VK_NULL_HANDLE;
            attribute_buffer_addr = 0;
        }
        if (index_buffer) {
            vmaDestroyBuffer(allocator_cache, index_buffer, index_allocation);
            index_buffer = VK_NULL_HANDLE;
            index_allocation = VK_NULL_HANDLE;
            index_buffer_addr = 0;
        }

        allocator_cache = VK_NULL_HANDLE;
    }
};

struct TextureSampler {
    VkDevice device_cache = VK_NULL_HANDLE;
    VmaAllocator allocator_cache = VK_NULL_HANDLE;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation image_allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t heap_index = 0xFFFFFFFFu;

    TextureSampler() = default;

    // Запрет копирования
    TextureSampler(const TextureSampler&) = delete;
    TextureSampler& operator=(const TextureSampler&) = delete;

    // Перемещение
    TextureSampler(TextureSampler&& other) noexcept {
        *this = std::move(other);
    }

    TextureSampler& operator=(TextureSampler&& other) noexcept {
        if (this != &other) {
            Destroy();

            device_cache = std::exchange(other.device_cache, VK_NULL_HANDLE);
            allocator_cache = std::exchange(other.allocator_cache, VK_NULL_HANDLE);

            image = std::exchange(other.image, VK_NULL_HANDLE);
            image_allocation = std::exchange(other.image_allocation, VK_NULL_HANDLE);
            view = std::exchange(other.view, VK_NULL_HANDLE);
            sampler = std::exchange(other.sampler, VK_NULL_HANDLE);
            heap_index = std::exchange(other.heap_index, 0xFFFFFFFFu);
        }
        return *this;
    }

    ~TextureSampler() {
        Destroy();
    }

    void Destroy() {
        if (view != VK_NULL_HANDLE || sampler != VK_NULL_HANDLE) {
            TRY_ASSERT(device_cache != VK_NULL_HANDLE,
                       "TextureSampler: device_cache is null, cannot destroy ImageView/Sampler!");

            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device_cache, view, nullptr);
                view = VK_NULL_HANDLE;
            }
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device_cache, sampler, nullptr);
                sampler = VK_NULL_HANDLE;
            }
        }

        if (image != VK_NULL_HANDLE) {
            if (allocator_cache != VK_NULL_HANDLE) {
                TRY_ASSERT(image_allocation != VK_NULL_HANDLE,
                           "TextureSampler: image_allocation is null! VMA memory leak detected!");
                vmaDestroyImage(allocator_cache, image, image_allocation);
                image = VK_NULL_HANDLE;
                image_allocation = VK_NULL_HANDLE;
            } else if (device_cache != VK_NULL_HANDLE) {
                vkDestroyImage(device_cache, image, nullptr);
                image = VK_NULL_HANDLE;
            }
        }

        device_cache = VK_NULL_HANDLE;
        allocator_cache = VK_NULL_HANDLE;
    }
};

struct MaterialTextureBinding {
    uint32_t binding = 0;
    resources::ResourceHandle<TextureSampler> texture;
};

struct MaterialBufferBinding {
    uint32_t binding = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize range = VK_WHOLE_SIZE;
};

inline uint32_t FindMemoryType(VkPhysicalDevice phys_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(phys_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    TRY_ASSERT(false, "Failed to find suitable Vulkan memory type!");
    return 0xFFFFFFFFu;
}

struct MaterialStorageBufferBinding {
    uint32_t binding = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize range = VK_WHOLE_SIZE;
};

struct CachedUBOBinding {
    uint32_t binding = 0;
    VkShaderStageFlags stage_flags = 0;
    uint32_t offset_in_buffer = 0;
    uint32_t size = 0;
};

// vkAllocateDescriptorSets/vkFreeDescriptorSets требуют внешней синхронизации на пуле,
// а материалы грузятся на пуле потоков и правятся из редактора на главном.
inline std::mutex& MaterialDescriptorMutex() {
    static std::mutex mutex;
    return mutex;
}

struct Material {
    explicit Material(resources::ResourceHandle<Shader> shdr_handle) : shader(std::move(shdr_handle)) {}

    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

    Material(Material&& other) noexcept {
        *this = std::move(other);
    }

    Material& operator=(Material&& other) noexcept {
        if (this != &other) {
            DestroyGpuResources();

            shader = std::move(other.shader);
            textures = std::move(other.textures);
            storage_buffers = std::move(other.storage_buffers);
            default_texture_view = other.default_texture_view;
            default_texture_sampler = other.default_texture_sampler;

            descriptor_set = std::exchange(other.descriptor_set, VK_NULL_HANDLE);
            retired_descriptor_sets = std::move(other.retired_descriptor_sets);
            descriptors_finalized = other.descriptors_finalized;
            uniform_buffer = std::exchange(other.uniform_buffer, VK_NULL_HANDLE);
            uniform_memory = std::exchange(other.uniform_memory, VK_NULL_HANDLE);
            mapped_uniform_ptr = std::exchange(other.mapped_uniform_ptr, nullptr);
            uniform_buffer_size = std::exchange(other.uniform_buffer_size, 0);
            device_cache = std::exchange(other.device_cache, VK_NULL_HANDLE);
        }
        return *this;
    }

    ~Material() {
        DestroyGpuResources();
    }

    resources::ResourceHandle<Shader> shader;

    // Ограниченные векторы предотвращают аллокации в куче и фиксируют размер структуры
    eastl::fixed_vector<MaterialTextureBinding, 8, false> textures;
    eastl::fixed_vector<MaterialBufferBinding, 2, false> storage_buffers;

    // Не владеет: 1x1 белая текстура принадлежит VulkanDevice. Подставляется в каждый
    // сэмплер set 1, который материал не привязал, иначе дескриптор остаётся невалидным.
    VkImageView default_texture_view = VK_NULL_HANDLE;
    VkSampler default_texture_sampler = VK_NULL_HANDLE;

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    // Наборы, которые уже могли попасть в записанные command buffer. Не освобождаем их
    // до уничтожения материала: живой материал меняет только указатель descriptor_set.
    eastl::vector<VkDescriptorSet> retired_descriptor_sets;
    bool descriptors_finalized = false;

    VkBuffer uniform_buffer = VK_NULL_HANDLE;
    VkDeviceMemory uniform_memory = VK_NULL_HANDLE;
    void* mapped_uniform_ptr = nullptr;
    uint32_t uniform_buffer_size = 0;

    VkDevice device_cache = VK_NULL_HANDLE;

    // Загрузчик вызывает после того, как все текстуры привязаны. С этого момента
    // изменение дескрипторов идёт только через новый набор, а не in-place.
    void FinalizeDescriptors() {
        descriptors_finalized = true;
    }

    // Текстура-заглушка для сэмплеров set 1, которые материал не привязал: шейдер всё
    // равно их читает, и дескриптор обязан быть валидным. Вызывать до InitGpuResources,
    // чтобы первый же UpdateDescriptors записал её в набор.
    void SetDefaultTexture(VkImageView view, VkSampler sampler) {
        default_texture_view = view;
        default_texture_sampler = sampler;
    }

    void InitGpuResources(VkDevice device, VkPhysicalDevice phys_device) {
        device_cache = device;

        // 1. Вычисляем общий размер Uniform Buffer из рефлексии шейдера
        uniform_buffer_size = 0;
        shader->reflection.ForEachMaterialBinding([this](const ShaderReflectedBinding& b) {
            if (b.kind == ShaderResourceKind::UniformBuffer)
                uniform_buffer_size += b.size;
        });

        if (uniform_buffer_size > 0) {
            VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            buffer_info.size = uniform_buffer_size;
            buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            TRY_CHECK(vkCreateBuffer(device, &buffer_info, nullptr, &uniform_buffer) == VK_SUCCESS,
                      "Failed to create Material Uniform buffer!");

            VkMemoryRequirements mem_reqs;
            vkGetBufferMemoryRequirements(device, uniform_buffer, &mem_reqs);

            VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            alloc_info.allocationSize = mem_reqs.size;
            alloc_info.memoryTypeIndex =
                FindMemoryType(phys_device, mem_reqs.memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            TRY_CHECK(vkAllocateMemory(device, &alloc_info, nullptr, &uniform_memory) == VK_SUCCESS,
                      "Failed to allocate Material Uniform memory!");

            vkBindBufferMemory(device, uniform_buffer, uniform_memory, 0);
            vkMapMemory(device, uniform_memory, 0, buffer_info.size, 0, &mapped_uniform_ptr);

            if (!shader->default_uniform_data.empty() && mapped_uniform_ptr) {
                std::memcpy(mapped_uniform_ptr, shader->default_uniform_data.data(),
                            std::min(static_cast<size_t>(uniform_buffer_size), shader->default_uniform_data.size()));
            }
        }

        // 2. Выделяем Set 1
        descriptor_set = AllocateDescriptorSet();
        if (descriptor_set == VK_NULL_HANDLE)
            LogError("Material: failed to allocate the initial descriptor set");
        else
            UpdateDescriptors();
    }

    void DestroyGpuResources() {
        if (device_cache == VK_NULL_HANDLE)
            return;

        if (mapped_uniform_ptr) {
            vkUnmapMemory(device_cache, uniform_memory);
            mapped_uniform_ptr = nullptr;
        }
        if (uniform_buffer) {
            vkDestroyBuffer(device_cache, uniform_buffer, nullptr);
            uniform_buffer = VK_NULL_HANDLE;
        }
        if (uniform_memory) {
            vkFreeMemory(device_cache, uniform_memory, nullptr);
            uniform_memory = VK_NULL_HANDLE;
        }
        if (shader->descriptor_pool) {
            std::lock_guard lock(MaterialDescriptorMutex());
            if (descriptor_set) {
                vkFreeDescriptorSets(device_cache, shader->descriptor_pool, 1, &descriptor_set);
                descriptor_set = VK_NULL_HANDLE;
            }
            for (VkDescriptorSet retired : retired_descriptor_sets)
                if (retired)
                    vkFreeDescriptorSets(device_cache, shader->descriptor_pool, 1, &retired);
        }
        retired_descriptor_sets.clear();
        descriptors_finalized = false;

        device_cache = VK_NULL_HANDLE;
    }

    VkDescriptorSet AllocateDescriptorSet() {
        constexpr uint32_t kMaterialSetIndex = static_cast<uint32_t>(BindingScope::Material);
        if (device_cache == VK_NULL_HANDLE || shader->descriptor_set_layouts.size() <= kMaterialSetIndex ||
            shader->descriptor_set_layouts[kMaterialSetIndex] == VK_NULL_HANDLE ||
            shader->descriptor_pool == VK_NULL_HANDLE) {
            return VK_NULL_HANDLE;
        }

        VkDescriptorSetAllocateInfo alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc_info.descriptorPool = shader->descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &shader->descriptor_set_layouts[kMaterialSetIndex];

        VkDescriptorSet set = VK_NULL_HANDLE;
        std::lock_guard lock(MaterialDescriptorMutex());
        if (vkAllocateDescriptorSets(device_cache, &alloc_info, &set) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return set;
    }

    template <typename T>
    void SetParam(eastl::string_view name, const T& value) {
        SetParamRaw(RGTagOf(name), &value, sizeof(T));
    }

    void SetParamRaw(RGTag tag, const void* data_ptr, uint32_t data_size) {
        if (!mapped_uniform_ptr || !data_ptr)
            return;

        // Прямой поиск смещения в рефлексии шейдера без промежуточных хэш-таблиц
        uint32_t current_offset = 0;
        shader->reflection.ForEachMaterialBinding([&](const ShaderReflectedBinding& b) {
            if (b.kind == ShaderResourceKind::UniformBuffer) {
                if (const auto* param = b.FindParamByTag(tag)) {
                    uint32_t final_offset = current_offset + param->offset;
                    if (final_offset + data_size <= uniform_buffer_size)
                        std::memcpy(static_cast<uint8_t*>(mapped_uniform_ptr) + final_offset, data_ptr, data_size);
                }
                current_offset += b.size;
            }
        });
    }

    void SetTexture(uint32_t binding, resources::ResourceHandle<TextureSampler> tex) {
        for (auto& bind : textures) {
            if (bind.binding == binding) {
                // Live-правка каждый кадр переустанавливает те же текстуры: без этой
                // проверки descriptor set перезаписывался бы на каждом изменении параметра.
                if (bind.texture == tex)
                    return;
                bind.texture = std::move(tex);
                UpdateDescriptors();
                return;
            }
        }
        textures.push_back({binding, std::move(tex)});
        UpdateDescriptors();
    }

    void SetTexture(eastl::string_view name, resources::ResourceHandle<TextureSampler> tex) {
        if (const auto* b = shader->reflection.FindBinding(name))
            SetTexture(b->binding, std::move(tex));
    }

    void SetStorageBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset = 0,
                          VkDeviceSize range = VK_WHOLE_SIZE) {
        for (auto& bind : storage_buffers) {
            if (bind.binding == binding) {
                if (bind.buffer == buffer && bind.offset == offset && bind.range == range)
                    return;
                bind.buffer = buffer;
                bind.offset = offset;
                bind.range = range;
                UpdateDescriptors();
                return;
            }
        }
        storage_buffers.push_back({binding, buffer, offset, range});
        UpdateDescriptors();
    }

    void SetStorageBuffer(eastl::string_view name, VkBuffer buffer, VkDeviceSize offset = 0,
                          VkDeviceSize range = VK_WHOLE_SIZE) {
        if (const auto* b = shader->reflection.FindBinding(name))
            SetStorageBuffer(b->binding, buffer, offset, range);
    }

    // Обновление дескрипторов созданного материала. До FinalizeDescriptors набор ещё
    // никому не отдан, поэтому его можно менять на месте. После — старый набор мог
    // попасть в записанный command buffer, и по VUID-vkUpdateDescriptorSets-None-03047
    // трогать его нельзя: выделяем новый и переносим привязки туда.
    void UpdateDescriptors() {
        if (descriptor_set == VK_NULL_HANDLE || device_cache == VK_NULL_HANDLE)
            return;

        if (descriptors_finalized) {
            VkDescriptorSet new_set = AllocateDescriptorSet();
            if (new_set == VK_NULL_HANDLE) {
                LogError("Material: failed to allocate a descriptor set for a live update");
                return;
            }
            retired_descriptor_sets.push_back(descriptor_set);
            descriptor_set = new_set;
        }

        WriteDescriptors(descriptor_set);
    }

    void WriteDescriptors(VkDescriptorSet set) {
        eastl::fixed_vector<VkWriteDescriptorSet, 16, false> writes;
        eastl::fixed_vector<VkDescriptorBufferInfo, 4, false> buffer_infos;
        eastl::fixed_vector<VkDescriptorImageInfo, 16, false> image_infos;

        // 1. Uniform Buffer
        if (uniform_buffer != VK_NULL_HANDLE) {
            shader->reflection.ForEachMaterialBinding([&](const ShaderReflectedBinding& b) {
                if (b.kind == ShaderResourceKind::UniformBuffer) {
                    auto& info = buffer_infos.push_back();
                    info.buffer = uniform_buffer;
                    info.offset = 0;
                    info.range = uniform_buffer_size;

                    auto& write = writes.push_back();
                    write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet = set;
                    write.dstBinding = b.binding;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    write.pBufferInfo = &info;
                }
            });
        }

        // 2. Textures
        for (const auto& bind : textures) {
            if (bind.texture) {
                auto& info = image_infos.push_back();
                info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                info.imageView = bind.texture->view;
                info.sampler = bind.texture->sampler;

                auto& write = writes.push_back();
                write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = set;
                write.dstBinding = bind.binding;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &info;
            }
        }

        // 3. Сэмплеры, которые материал не привязал, получают текстуру-заглушку: шейдер
        // статически читает их, и валидация требует обновлённый дескриптор на каждый draw.
        if (default_texture_view != VK_NULL_HANDLE && default_texture_sampler != VK_NULL_HANDLE) {
            shader->reflection.ForEachMaterialBinding([&](const ShaderReflectedBinding& b) {
                if (b.kind != ShaderResourceKind::SampledTexture)
                    return;

                for (const auto& bind : textures)
                    if (bind.binding == b.binding && bind.texture)
                        return;

                auto& info = image_infos.push_back();
                info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                info.imageView = default_texture_view;
                info.sampler = default_texture_sampler;

                auto& write = writes.push_back();
                write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = set;
                write.dstBinding = b.binding;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &info;
            });
        }

        // 4. Storage Buffers
        for (const auto& bind : storage_buffers) {
            if (bind.buffer != VK_NULL_HANDLE) {
                auto& info = buffer_infos.push_back();
                info.buffer = bind.buffer;
                info.offset = bind.offset;
                info.range = bind.range;

                auto& write = writes.push_back();
                write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = set;
                write.dstBinding = bind.binding;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write.pBufferInfo = &info;
            }
        }

        if (!writes.empty())
            vkUpdateDescriptorSets(device_cache, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
};

}  // namespace tryengine::graphics