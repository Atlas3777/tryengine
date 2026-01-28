#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

struct Vertex {
    float x, y, z;
    float r, g, b, a;
    float u, v; // Добавляем UV
};

static Vertex vertices[] = {
    {-0.5f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 0.0f}, // Лево-верх
    { 0.5f,  0.5f, 0.0f,  0.0f, 1.0f, 0.0f, 1.0f,  1.0f, 0.0f}, // Право-верх
    { 0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f}, // Право-низ
    {-0.5f, -0.5f, 0.0f,  1.0f, 1.0f, 0.0f, 1.0f,  0.0f, 1.0f}  // Лево-низ
};

static Uint16 indices[] = {
    0, 1, 2, // Первый треугольник
    0, 2, 3  // Второй треугольник
};

struct UniformBufferVertex {
    float srcAspect;
};

constexpr float WindowWidth = 960;
constexpr float WindowHeight = 540;

SDL_Window *window;
SDL_GPUDevice *device;
SDL_GPUTransferBuffer *transferBuffer;
SDL_GPUBuffer *vertexBuffer;
SDL_GPUBuffer *indexBuffer;

int main(int argc, char *argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return -1;

    window = SDL_CreateWindow("XPBD Engine - Triangle", WindowWidth, WindowHeight, 0);
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);

    SDL_ClaimWindowForGPUDevice(device, window);

    size_t vertexCodeSize;
    void *vertexCode = SDL_LoadFile("shaders/vertex.spv", &vertexCodeSize);

    SDL_GPUShaderCreateInfo vertexInfo;
    vertexInfo.code = (Uint8 *) vertexCode;
    vertexInfo.code_size = vertexCodeSize;
    vertexInfo.entrypoint = "main";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexInfo.num_samplers = 0;
    vertexInfo.num_storage_buffers = 0;
    vertexInfo.num_storage_textures = 0;
    vertexInfo.num_uniform_buffers = 1;

    SDL_GPUShader *vertexShader = SDL_CreateGPUShader(device, &vertexInfo);
    SDL_free(vertexCode);

    size_t fragmentCodeSize;
    void *fragmentCode = SDL_LoadFile("shaders/fragment.spv", &fragmentCodeSize);

    SDL_GPUShaderCreateInfo fragmentInfo{};
    fragmentInfo.code = (Uint8 *) fragmentCode;
    fragmentInfo.code_size = fragmentCodeSize;
    fragmentInfo.entrypoint = "main";
    fragmentInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentInfo.num_samplers = 0;
    fragmentInfo.num_storage_buffers = 0;
    fragmentInfo.num_storage_textures = 0;
    fragmentInfo.num_uniform_buffers = 1;

    SDL_GPUShader *fragmentShader = SDL_CreateGPUShader(device, &fragmentInfo);
    SDL_free(fragmentCode);


    // stbi_set_flip_vertically_on_load(true);
    int width, height, channels;
    unsigned char *imageData = stbi_load("assets/test.png", &width, &height, &channels, STBI_rgb_alpha);
    if (!imageData) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load image: %s", stbi_failure_reason());
        return -1;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    SDL_GPUVertexBufferDescription vertexBufferDescriptions[1];
    vertexBufferDescriptions[0].slot = 0;
    vertexBufferDescriptions[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertexBufferDescriptions[0].instance_step_rate = 0;
    vertexBufferDescriptions[0].pitch = sizeof(Vertex);

    SDL_GPUVertexAttribute vertexAttributes[3];

    vertexAttributes[0].buffer_slot = 0;
    vertexAttributes[0].location = 0;
    vertexAttributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertexAttributes[0].offset = 0;

    vertexAttributes[1].buffer_slot = 0;
    vertexAttributes[1].location = 1;
    vertexAttributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    vertexAttributes[1].offset = sizeof(float) * 3;

    vertexAttributes[2].buffer_slot = 0;
    vertexAttributes[2].location = 2;
    vertexAttributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertexAttributes[2].offset = sizeof(float) * 7;

    SDL_GPUColorTargetDescription colorTargetDescriptions[1];
    colorTargetDescriptions[0] = {};
    colorTargetDescriptions[0].blend_state.enable_blend = true;
    colorTargetDescriptions[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTargetDescriptions[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTargetDescriptions[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorTargetDescriptions[0].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTargetDescriptions[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorTargetDescriptions[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTargetDescriptions[0].format = SDL_GetGPUSwapchainTextureFormat(device, window);

    pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexBufferDescriptions;
    pipelineInfo.vertex_input_state.vertex_attributes = vertexAttributes;

    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.color_target_descriptions = colorTargetDescriptions;


    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.size = sizeof(vertices);
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vertexBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);


    SDL_GPUBufferCreateInfo indexBufInfo{};
    indexBufInfo.size = sizeof(indices);
    indexBufInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    indexBuffer = SDL_CreateGPUBuffer(device, &indexBufInfo);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.size = sizeof(vertices) + sizeof(indices);
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

    Uint8* mapData = (Uint8*)SDL_MapGPUTransferBuffer(device, transferBuffer, false);
    SDL_memcpy(mapData, vertices, sizeof(vertices));
    SDL_memcpy(mapData + sizeof(vertices), indices, sizeof(indices));

    SDL_UnmapGPUTransferBuffer(device, transferBuffer);

    // Копирование из Transfer в Vertex Buffer
    SDL_GPUCommandBuffer *uploadCmd = SDL_AcquireGPUCommandBuffer(device);

    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(uploadCmd);

    SDL_GPUTransferBufferLocation srcVert{transferBuffer, 0};
    SDL_GPUBufferRegion dstVert{vertexBuffer, 0, sizeof(vertices)};
    SDL_UploadToGPUBuffer(copyPass, &srcVert, &dstVert, true);

    SDL_GPUTransferBufferLocation srcIdx{transferBuffer, sizeof(vertices)};
    SDL_GPUBufferRegion dstIdx{indexBuffer, 0, sizeof(indices)};
    SDL_UploadToGPUBuffer(copyPass, &srcIdx, &dstIdx, true);

    SDL_EndGPUCopyPass(copyPass);

    SDL_SubmitGPUCommandBuffer(uploadCmd);

    // --- ЦИКЛ ---
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
        }

        SDL_GPUCommandBuffer *commandBuffer = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUTexture *swapchainTexture;

        Uint32 width, height;
        SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window, &swapchainTexture, &width, &height);

        if (swapchainTexture == NULL)
        {
            SDL_SubmitGPUCommandBuffer(commandBuffer);
            return SDL_APP_CONTINUE;
        }

        SDL_GPUColorTargetInfo colorTargetInfo{};
        colorTargetInfo.clear_color = {240/255.0f, 240/255.0f, 240/255.0f, 255/255.0f};
        colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
        colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;
        colorTargetInfo.texture = swapchainTexture;


        UniformBufferVertex uVertexBuffer{};
        uVertexBuffer.srcAspect = WindowHeight/WindowWidth;
        SDL_PushGPUVertexUniformData(commandBuffer, 0 , &uVertexBuffer, sizeof(UniformBufferVertex));

        // begin a render pass
        SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorTargetInfo, 1, NULL);


        SDL_BindGPUGraphicsPipeline(renderPass, pipeline);

        SDL_GPUBufferBinding vertexBinding{vertexBuffer, 0};
        SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBinding, 1);

        SDL_GPUBufferBinding indexBinding{indexBuffer, 0};

        SDL_BindGPUIndexBuffer(renderPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);


        SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);

        // end the render pass
        SDL_EndGPURenderPass(renderPass);

        // submit the command buffer
        SDL_SubmitGPUCommandBuffer(commandBuffer);
    }

    // Cleanup
    SDL_ReleaseGPUBuffer(device, vertexBuffer);
    SDL_ReleaseGPUBuffer(device, indexBuffer);
    SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
