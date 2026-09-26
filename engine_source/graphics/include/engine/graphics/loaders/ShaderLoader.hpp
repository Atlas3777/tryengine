#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <volk.h>

#include <EASTL/hash_map.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "engine/async/Task.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/graphics/ShaderBinary.hpp"

namespace tryengine::graphics {

class ShaderLoader {
public:
    explicit ShaderLoader(VkDevice device) : device_(device) {}

    async::Task<Shader> Parse(eastl::span<const uint8_t> data) const {
        LogInfo("Shader Load...");

        co_await tryengine::async::ExecutorSwitch(tryengine::async::MainThread());

        auto unpack_result = UnpackShaderBinary(data);
        if (!unpack_result.has_value()) {
            co_return LogAndMakeError("Failed to unpack ShaderBinary container");
        }

        auto& binary_content = *unpack_result;

        Shader shader(device_);
        shader.reflection = std::move(binary_content.reflection);

        // 1. Группируем и MERGE биндинги по (set, binding)
        eastl::hash_map<uint32_t, eastl::vector<VkDescriptorSetLayoutBinding>> set_bindings_map;
        VkShaderStageFlags per_obj_stages = 0;

        for (const auto& b : shader.reflection.bindings) {
            VkShaderStageFlags vk_stage_flags = MapToVkShaderStageFlags(b.stage_flags);

            if (b.IsPerObj()) {
                per_obj_stages |= vk_stage_flags;
            }

            auto& bindings_vec = set_bindings_map[b.set];
            bool found = false;
            for (auto& existing : bindings_vec) {
                if (existing.binding == b.binding) {
                    existing.stageFlags |= vk_stage_flags;
                    found = true;
                    break;
                }
            }
            if (!found) {
                VkDescriptorSetLayoutBinding layout_binding{};
                layout_binding.binding = b.binding;
                layout_binding.descriptorCount = 1;
                layout_binding.descriptorType = MapResourceKindToVkDescriptorType(b.kind);
                layout_binding.stageFlags = vk_stage_flags;
                bindings_vec.push_back(layout_binding);
            }
        }

        shader.per_obj_push_stages = (per_obj_stages != 0) ? per_obj_stages : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

        // 2. Создаём VkDescriptorSetLayout
        if (!set_bindings_map.empty()) {
            uint32_t max_set = 0;
            for (const auto& [set_idx, _] : set_bindings_map) {
                if (set_idx > max_set) max_set = set_idx;
            }

            shader.descriptor_set_layouts.resize(max_set + 1, VK_NULL_HANDLE);

            for (uint32_t set_idx = 0; set_idx <= max_set; ++set_idx) {
                auto it = set_bindings_map.find(set_idx);
                VkDescriptorSetLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

                if (it != set_bindings_map.end()) {
                    layout_info.bindingCount = static_cast<uint32_t>(it->second.size());
                    layout_info.pBindings = it->second.data();
                }

                if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &shader.descriptor_set_layouts[set_idx]) != VK_SUCCESS) {
                    shader.Destroy(device_);
                    co_return LogAndMakeError("Failed to create VkDescriptorSetLayout");
                }
            }
        } else {
            shader.descriptor_set_layouts.clear();
        }

        // 3. Создаём VkPipelineLayout
        VkPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = shader.per_obj_push_stages;
        push_constant_range.offset = 0;
        push_constant_range.size = 128;

        VkPipelineLayoutCreateInfo pipeline_layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(shader.descriptor_set_layouts.size());
        pipeline_layout_info.pSetLayouts = shader.descriptor_set_layouts.data();
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_constant_range;

        if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &shader.layout) != VK_SUCCESS) {
            shader.Destroy(device_);
            co_return LogAndMakeError("Failed to create VkPipelineLayout");
        }

        // 4. Пул дескрипторов
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128 }
        };

        VkDescriptorPoolCreateInfo pool_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 256;
        pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &shader.descriptor_pool) != VK_SUCCESS) {
            shader.Destroy(device_);
            co_return LogAndMakeError("Failed to create VkDescriptorPool");
        }

        // 5. Создаём VkShaderEXT (указываем pushConstantRanges явным образом)
        VkShaderCreateInfoEXT vs_info{ VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
        vs_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vs_info.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
        vs_info.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
        vs_info.codeSize = binary_content.vertex_spv.size();
        vs_info.pCode = binary_content.vertex_spv.data();
        vs_info.pName = "main";
        vs_info.setLayoutCount = static_cast<uint32_t>(shader.descriptor_set_layouts.size());
        vs_info.pSetLayouts = shader.descriptor_set_layouts.data();
        vs_info.pushConstantRangeCount = 1;
        vs_info.pPushConstantRanges = &push_constant_range;

        if (vkCreateShadersEXT(device_, 1, &vs_info, nullptr, &shader.vertex_shader) != VK_SUCCESS) {
            shader.Destroy(device_);
            co_return LogAndMakeError("Failed to create Vertex VkShaderEXT");
        }

        VkShaderCreateInfoEXT fs_info{ VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
        fs_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fs_info.nextStage = 0;
        fs_info.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
        fs_info.codeSize = binary_content.fragment_spv.size();
        fs_info.pCode = binary_content.fragment_spv.data();
        fs_info.pName = "main";
        fs_info.setLayoutCount = static_cast<uint32_t>(shader.descriptor_set_layouts.size());
        fs_info.pSetLayouts = shader.descriptor_set_layouts.data();
        fs_info.pushConstantRangeCount = 1;
        fs_info.pPushConstantRanges = &push_constant_range;

        if (vkCreateShadersEXT(device_, 1, &fs_info, nullptr, &shader.fragment_shader) != VK_SUCCESS) {
            shader.Destroy(device_);
            co_return LogAndMakeError("Failed to create Fragment VkShaderEXT");
        }

        LogInfo("End Shader Loading");

        co_return eastl::move(shader);
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
};

}  // namespace tryengine::graphics