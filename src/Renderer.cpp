#include "Renderer.hpp"

#include <SDL3/SDL_gpu.h>

#include <glm/gtc/matrix_inverse.hpp>

#include "EngineTypes.hpp"

void Renderer::Init(SDL_GPUDevice* device) {
    SDL_GPUShader* vertexShader = CreateVertexShader(*device);
    SDL_GPUShader* fragmentShader = CreateFragmentShader(*device);
    this->commonSampler = CreateSampler(*device);

    SDL_GPUVertexBufferDescription vertexBufferDescriptions = CreateVertexBufferDescription();
    SDL_GPUVertexAttribute vertexAttributes[4];
    SetupVertexAttributes(vertexAttributes);

    SDL_GPUColorTargetDescription colorTargetDescriptions[1];
    SetupColorTargetDescription(colorTargetDescriptions);
    this->pipeline = CreateGraphicsPipeline(*device, vertexShader, fragmentShader, &vertexBufferDescriptions,
                                            vertexAttributes, colorTargetDescriptions);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);
}

void Renderer::Cleanup() {
    if (commonSampler) SDL_ReleaseGPUSampler(device, commonSampler);
    if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
}

SDL_GPURenderPass* Renderer::BeginRenderPass(SDL_GPUCommandBuffer* cmd, RenderTarget& target, SDL_FColor clearColor) {
    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = target.GetColor();
    colorInfo.clear_color = clearColor;
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depthInfo{};
    depthInfo.texture = target.GetDepth();
    depthInfo.clear_depth = 1.0f;
    depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    depthInfo.store_op = SDL_GPU_STOREOP_STORE;
    depthInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    return SDL_BeginGPURenderPass(cmd, &colorInfo, 1, &depthInfo);
}

SDL_GPURenderPass* Renderer::BeginRenderToWindow(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain) {
    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = swapchain;
    colorInfo.clear_color = {0, 0, 0, 1};
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;
    return SDL_BeginGPURenderPass(cmd, &colorInfo, 1, nullptr);
};

SDL_GPUShader* Renderer::CreateVertexShader(SDL_GPUDevice& device) {
    size_t vertexCodeSize;
    void* vertexCode = SDL_LoadFile("shaders/vertex.spv", &vertexCodeSize);

    SDL_GPUShaderCreateInfo vertexInfo;
    vertexInfo.code = (Uint8*)vertexCode;
    vertexInfo.code_size = vertexCodeSize;
    vertexInfo.entrypoint = "main";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexInfo.num_samplers = 0;
    vertexInfo.num_storage_buffers = 0;
    vertexInfo.num_storage_textures = 0;
    vertexInfo.num_uniform_buffers = 1;

    SDL_GPUShader* vertexShader = SDL_CreateGPUShader(&device, &vertexInfo);
    SDL_free(vertexCode);
    return vertexShader;
}

SDL_GPUShader* Renderer::CreateFragmentShader(SDL_GPUDevice& device) {
    size_t fragmentCodeSize;
    void* fragmentCode = SDL_LoadFile("shaders/fragment.spv", &fragmentCodeSize);

    SDL_GPUShaderCreateInfo fragmentInfo{};
    fragmentInfo.code = (Uint8*)fragmentCode;
    fragmentInfo.code_size = fragmentCodeSize;
    fragmentInfo.entrypoint = "main";
    fragmentInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentInfo.num_samplers = 1;
    fragmentInfo.num_storage_buffers = 0;
    fragmentInfo.num_storage_textures = 0;
    fragmentInfo.num_uniform_buffers = 1;

    SDL_GPUShader* fragmentShader = SDL_CreateGPUShader(&device, &fragmentInfo);
    SDL_free(fragmentCode);
    return fragmentShader;
}

void Renderer::SetupVertexAttributes(SDL_GPUVertexAttribute* attributes) {
    // 0: Position (Float3)
    attributes[0].buffer_slot = 0;
    attributes[0].location = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = 0;

    // 1: Normal (Float3)
    attributes[1].buffer_slot = 0;
    attributes[1].location = 1;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = sizeof(float) * 3;  // Сразу после x,y,z

    // 2: Color (Float4)
    attributes[2].buffer_slot = 0;
    attributes[2].location = 2;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[2].offset = sizeof(float) * 6;  // Pos(3) + Normal(3)

    // 3: UV (Float2)
    attributes[3].buffer_slot = 0;
    attributes[3].location = 3;
    attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[3].offset = sizeof(float) * 10;  // Pos(3) + Normal(3) + Color(4)
}

void Renderer::SetupColorTargetDescription(SDL_GPUColorTargetDescription* colorTargetDesc) {
    colorTargetDesc[0] = {};
    colorTargetDesc[0].blend_state.enable_blend = true;
    colorTargetDesc[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTargetDesc[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTargetDesc[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorTargetDesc[0].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTargetDesc[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorTargetDesc[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTargetDesc[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

SDL_GPUSampler* Renderer::CreateSampler(SDL_GPUDevice& device) {
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    return SDL_CreateGPUSampler(&device, &samplerInfo);
}

SDL_GPUVertexBufferDescription Renderer::CreateVertexBufferDescription() {
    SDL_GPUVertexBufferDescription vertexBufferDesc;
    vertexBufferDesc.slot = 0;
    vertexBufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertexBufferDesc.instance_step_rate = 0;
    vertexBufferDesc.pitch = sizeof(Vertex);
    return vertexBufferDesc;
}

SDL_GPUGraphicsPipeline* Renderer::CreateGraphicsPipeline(SDL_GPUDevice& device, SDL_GPUShader* vertexShader,
                                                          SDL_GPUShader* fragmentShader,
                                                          const SDL_GPUVertexBufferDescription* vertexBufferDesc,
                                                          const SDL_GPUVertexAttribute* vertexAttributes,
                                                          const SDL_GPUColorTargetDescription* colorTargetDesc) {
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    // pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_LINESTRIP;
    // pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;

    pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    pipelineInfo.vertex_input_state.num_vertex_attributes = 4;
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexBufferDesc;
    pipelineInfo.vertex_input_state.vertex_attributes = vertexAttributes;

    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.color_target_descriptions = colorTargetDesc;

    pipelineInfo.depth_stencil_state.enable_depth_test = true;
    pipelineInfo.depth_stencil_state.enable_depth_write = true;
    // pipelineInfo.depth_stencil_state.enable_depth_write = false;
    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipelineInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    pipelineInfo.target_info.has_depth_stencil_target = true;

    return SDL_CreateGPUGraphicsPipeline(&device, &pipelineInfo);
}
