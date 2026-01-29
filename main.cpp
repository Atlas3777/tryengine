#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_oldnames.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <vector>
#include <string>

struct Vertex {
  float x, y, z;      // Position
  float nx, ny, nz;   // Normal (Добавили для освещения)
  float r, g, b, a;   // Color (Оставим пока как заглушку или tint)
  float u, v;         // UV
};

static Vertex vertices[] = {
    //   x,     y,     z,      nx,   ny,   nz,   r,    g,    b,    a,    u,    v
    {-0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f, 1.0f,  0.0f, 0.0f}, // 0
    {-0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 1.0f}, // 1
    {-0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 0.0f}, // 2
    {-0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f, 1.0f,  1.0f, 1.0f}, // 3
    { 0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f, 1.0f,  0.0f, 0.0f}, // 4
    { 0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f,  0.0f, 1.0f}, // 5
    { 0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f,  1.0f, 0.0f}, // 6
    { 0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f, 1.0f,  1.0f, 1.0f}  // 7
};

static Uint16 indices[] = {
    0, 1, 4, 1, 5, 4, // bottom
    2, 7, 3, 2, 6, 7, // top
    0, 6, 2, 0, 4, 6, // front
    1, 3, 7, 1, 5, 7, // back
    0, 2, 3, 0, 3, 1, // left
    4, 7, 6, 4, 5, 7  // right
};

bool LoadMesh(const std::string& path, std::vector<Vertex>& outVertices, std::vector<Uint16>& outIndices) {
    Assimp::Importer importer;

    // Флаги:
    // aiProcess_Triangulate: превращает всё в треугольники (обязательно для GPU)
    // aiProcess_FlipUVs: переворачивает Y у текстур (зависит от того, как ты экспортишь, для OpenGL часто нужно, для Vulkan/SDL3 иногда нет. Попробуй с ним и без)
    // aiProcess_GenNormals: генерирует нормали, если их нет в файле
    // aiProcess_JoinIdenticalVertices: оптимизация, объединяет полностью одинаковые вершины
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenNormals |
        aiProcess_JoinIdenticalVertices);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Assimp Error: %s", importer.GetErrorString());
        return false;
    }

    // Берем первый меш из файла (для простоты пока так)
    if (scene->mNumMeshes == 0) return false;
    aiMesh* mesh = scene->mMeshes[0];

    // 1. Парсим вершины
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        Vertex vertex{};

        // Позиции
        vertex.x = mesh->mVertices[i].x;
        vertex.y = mesh->mVertices[i].y;
        vertex.z = mesh->mVertices[i].z;

        // Нормали (если есть)
        if (mesh->HasNormals()) {
            vertex.nx = mesh->mNormals[i].x;
            vertex.ny = mesh->mNormals[i].y;
            vertex.nz = mesh->mNormals[i].z;
        } else {
            vertex.nx = 0.0f;
            vertex.ny = 1.0f;
            vertex.nz = 0.0f;
        }

        // Цвета (если есть, иначе белый)
        if (mesh->HasVertexColors(0)) {
            vertex.r = mesh->mColors[0][i].r;
            vertex.g = mesh->mColors[0][i].g;
            vertex.b = mesh->mColors[0][i].b;
            vertex.a = mesh->mColors[0][i].a;
        } else {
            vertex.r = 1.0f; vertex.g = 1.0f; vertex.b = 1.0f; vertex.a = 1.0f;
        }

        // Текстурные координаты (UV)
        if (mesh->HasTextureCoords(0)) {
            vertex.u = mesh->mTextureCoords[0][i].x;
            vertex.v = mesh->mTextureCoords[0][i].y;
        } else {
            vertex.u = 0.0f;
            vertex.v = 0.0f;
        }

        outVertices.push_back(vertex);
    }

    // 2. Парсим индексы
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            outIndices.push_back(static_cast<Uint16>(face.mIndices[j]));
        }
    }

    SDL_Log("Loaded Mesh: %s (Verts: %zu, Indices: %zu)", path.c_str(), outVertices.size(), outIndices.size());
    return true;
}

struct alignas(16) UniformBufferVertex {
  glm::mat4 mvp;
};

constexpr float WindowWidth = 960;
constexpr float WindowHeight = 540;

SDL_Window *window;
SDL_GPUDevice *device;
SDL_GPUTransferBuffer *transferBuffer;
SDL_GPUBuffer *vertexBuffer;
SDL_GPUBuffer *indexBuffer;

int main(int argc, char *argv[]) {
  if (!SDL_Init(SDL_INIT_VIDEO))
    return -1;

  window = SDL_CreateWindow("tryengine", WindowWidth, WindowHeight, 0);
  device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);

  SDL_ClaimWindowForGPUDevice(device, window);

  size_t vertexCodeSize;
  void *vertexCode = SDL_LoadFile("shaders/vertex.spv", &vertexCodeSize);

  SDL_GPUShaderCreateInfo vertexInfo;
  vertexInfo.code = (Uint8 *)vertexCode;
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
  fragmentInfo.code = (Uint8 *)fragmentCode;
  fragmentInfo.code_size = fragmentCodeSize;
  fragmentInfo.entrypoint = "main";
  fragmentInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
  fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  fragmentInfo.num_samplers = 1;
  fragmentInfo.num_storage_buffers = 0;
  fragmentInfo.num_storage_textures = 0;
  fragmentInfo.num_uniform_buffers = 1;

  SDL_GPUShader *fragmentShader = SDL_CreateGPUShader(device, &fragmentInfo);
  SDL_free(fragmentCode);

  SDL_GPUTextureCreateInfo depthTexInfo{};
  depthTexInfo.type = SDL_GPU_TEXTURETYPE_2D;
  depthTexInfo.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM; // Или D32_SFLOAT
  depthTexInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  depthTexInfo.width = WindowWidth;
  depthTexInfo.height = WindowHeight;
  depthTexInfo.layer_count_or_depth = 1;
  depthTexInfo.num_levels = 1;
  depthTexInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

  SDL_GPUTexture* depthTexture = SDL_CreateGPUTexture(device, &depthTexInfo);

  // stbi_set_flip_vertically_on_load(true);
  int width, height, channels;
  unsigned char *imageData =
      stbi_load("assets/test.png", &width, &height, &channels, STBI_rgb_alpha);
  if (!imageData) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load image: %s",
                 stbi_failure_reason());
    return -1;
  }
  SDL_GPUTextureCreateInfo texInfo{};
  texInfo.type = SDL_GPU_TEXTURETYPE_2D;
  texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER; // будем семплить
  texInfo.width = (Uint32)width;
  texInfo.height = (Uint32)height;
  texInfo.layer_count_or_depth = 1;
  texInfo.num_levels = 1;
  texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
  texInfo.props = 0;

  SDL_GPUTexture *gpuTexture = SDL_CreateGPUTexture(device, &texInfo);
  if (!gpuTexture) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CreateGPUTexture failed");
    stbi_image_free(imageData);
    return -1;
  }

  // create a transfer buffer just for the image upload
  SDL_GPUTransferBufferCreateInfo transferTexInfo{};
  transferTexInfo.size = (Uint64)width * (Uint64)height * 4; // RGBA8
  transferTexInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  SDL_GPUTransferBuffer *transferBufferTex =
      SDL_CreateGPUTransferBuffer(device, &transferTexInfo);

  // fill transfer buffer with image data
  Uint8 *mapTex =
      (Uint8 *)SDL_MapGPUTransferBuffer(device, transferBufferTex, false);
  SDL_memcpy(mapTex, imageData, (size_t)transferTexInfo.size);
  SDL_UnmapGPUTransferBuffer(device, transferBufferTex);

  // we no longer need CPU-side pixels after copying to transfer buffer
  stbi_image_free(imageData);
  imageData = NULL;

  SDL_GPUSamplerCreateInfo samplerInfo{};
  samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
  samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
  samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

  SDL_GPUSampler *gpuSampler = SDL_CreateGPUSampler(device, &samplerInfo);

  SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.vertex_shader = vertexShader;
  pipelineInfo.fragment_shader = fragmentShader;
  pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  SDL_GPUVertexBufferDescription vertexBufferDescriptions[1];
  vertexBufferDescriptions[0].slot = 0;
  vertexBufferDescriptions[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  vertexBufferDescriptions[0].instance_step_rate = 0;
  vertexBufferDescriptions[0].pitch = sizeof(Vertex);

  // Теперь у нас 4 атрибута: Pos, Normal, Color, UV
  SDL_GPUVertexAttribute vertexAttributes[4];

  // 0: Position (Float3)
  vertexAttributes[0].buffer_slot = 0;
  vertexAttributes[0].location = 0;
  vertexAttributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  vertexAttributes[0].offset = 0;

  // 1: Normal (Float3) - НОВОЕ
  vertexAttributes[1].buffer_slot = 0;
  vertexAttributes[1].location = 1;
  vertexAttributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  vertexAttributes[1].offset = sizeof(float) * 3; // Сразу после x,y,z

  // 2: Color (Float4)
  vertexAttributes[2].buffer_slot = 0;
  vertexAttributes[2].location = 2;
  vertexAttributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
  vertexAttributes[2].offset = sizeof(float) * 6; // Pos(3) + Normal(3)

  // 3: UV (Float2)
  vertexAttributes[3].buffer_slot = 0;
  vertexAttributes[3].location = 3;
  vertexAttributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
  vertexAttributes[3].offset = sizeof(float) * 10; // Pos(3) + Normal(3) + Color(4)

  SDL_GPUColorTargetDescription colorTargetDescriptions[1];
  colorTargetDescriptions[0] = {};
  colorTargetDescriptions[0].blend_state.enable_blend = true;
  colorTargetDescriptions[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTargetDescriptions[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTargetDescriptions[0].blend_state.src_color_blendfactor =
      SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  colorTargetDescriptions[0].blend_state.dst_color_blendfactor =
      SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTargetDescriptions[0].blend_state.src_alpha_blendfactor =
      SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  colorTargetDescriptions[0].blend_state.dst_alpha_blendfactor =
      SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTargetDescriptions[0].format =
      SDL_GetGPUSwapchainTextureFormat(device, window);

  pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
  pipelineInfo.vertex_input_state.num_vertex_attributes = 4; // БЫЛО 3
  pipelineInfo.vertex_input_state.vertex_buffer_descriptions =
      vertexBufferDescriptions;
  pipelineInfo.vertex_input_state.vertex_attributes = vertexAttributes;

  pipelineInfo.target_info.num_color_targets = 1;
  pipelineInfo.target_info.color_target_descriptions = colorTargetDescriptions;

  pipelineInfo.depth_stencil_state.enable_depth_test = true;
  pipelineInfo.depth_stencil_state.enable_depth_write = true;
  pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  pipelineInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
  pipelineInfo.target_info.has_depth_stencil_target = true;

  SDL_GPUGraphicsPipeline *pipeline =
      SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);

  SDL_ReleaseGPUShader(device, vertexShader);
  SDL_ReleaseGPUShader(device, fragmentShader);

  // Загрузка модели с помощью Assimp
  std::vector<Vertex> meshVertices;
  std::vector<Uint16> meshIndices;

  // Загружаем модель из assets/box_low_poly/scene.gltf
  if (!LoadMesh("assets/box_low_poly/scene.gltf", meshVertices, meshIndices)) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load model");
      return -1;
  }

  // Создание буферов с динамическими размерами
  SDL_GPUBufferCreateInfo bufferInfo{};
  bufferInfo.size = meshVertices.size() * sizeof(Vertex); // Динамический размер
  bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  vertexBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);

  SDL_GPUBufferCreateInfo indexBufInfo{};
  indexBufInfo.size = meshIndices.size() * sizeof(Uint16); // Динамический размер
  indexBufInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
  indexBuffer = SDL_CreateGPUBuffer(device, &indexBufInfo);

  SDL_GPUTransferBufferCreateInfo transferInfo{};
  transferInfo.size = bufferInfo.size + indexBufInfo.size;
  transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

  Uint8 *mapData = (Uint8 *)SDL_MapGPUTransferBuffer(device, transferBuffer, false);

  // Копируем вектора
  SDL_memcpy(mapData, meshVertices.data(), bufferInfo.size);
  SDL_memcpy(mapData + bufferInfo.size, meshIndices.data(), indexBufInfo.size);

  SDL_UnmapGPUTransferBuffer(device, transferBuffer);

  // Копирование из Transfer в Vertex Buffer
  SDL_GPUCommandBuffer *uploadCmd = SDL_AcquireGPUCommandBuffer(device);

  SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(uploadCmd);

  SDL_GPUTransferBufferLocation srcVert{transferBuffer, 0};
  SDL_GPUBufferRegion dstVert{vertexBuffer, 0, bufferInfo.size};
  SDL_UploadToGPUBuffer(copyPass, &srcVert, &dstVert, true);

  // Смещение для индексов теперь динамическое
  SDL_GPUTransferBufferLocation srcIdx{transferBuffer, bufferInfo.size};
  SDL_GPUBufferRegion dstIdx{indexBuffer, 0, indexBufInfo.size};
  SDL_UploadToGPUBuffer(copyPass, &srcIdx, &dstIdx, true);

  SDL_GPUTextureTransferInfo srcTexInfo{
      transferBufferTex, 0, 0,
      0}; // pixels_per_row / rows_per_layer = 0 => tightly packed
  SDL_GPUTextureRegion dstTexRegion{
      gpuTexture, 0, 0, 0, 0, 0, (Uint32)width, (Uint32)height, 1};
  SDL_UploadToGPUTexture(copyPass, &srcTexInfo, &dstTexRegion, true);

  SDL_EndGPUCopyPass(copyPass);

  SDL_SubmitGPUCommandBuffer(uploadCmd);

  SDL_ReleaseGPUTransferBuffer(device, transferBufferTex);
  transferBufferTex = NULL;

  glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 0.0f);
  glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
  glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

  uint64_t lastTime = SDL_GetTicksNS();
  double deltaTime = 0.0;
  float cameraSpeedBase = 2.5f;

  bool firstMouse = true;
  float pitch = 0.0f;
  float yaw = -90.0f;
  float lastX = WindowWidth / 2;
  float lastY = WindowHeight / 2;

  SDL_SetWindowRelativeMouseMode(window, true);

  // --- ЦИКЛ ---
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT)
        running = false;
      else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (firstMouse) {
          firstMouse = false;
          continue; // пропускаем первый рывок
        }
        float xoffset = event.motion.xrel;
        float yoffset =
            -event.motion.yrel; // инвертируем, т.к. Y идет сверху вниз

        float sensitivity = 0.1f;
        xoffset *= sensitivity;
        yoffset *= sensitivity;

        yaw += xoffset;
        pitch += yoffset;

        // Ограничиваем pitch, чтобы не "сделать сальто"
        if (pitch > 89.0f)
          pitch = 89.0f;
        if (pitch < -89.0f)
          pitch = -89.0f;

        // Пересчитываем вектор направления камеры
        glm::vec3 front;
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        cameraFront = glm::normalize(front);
      }
    }

    uint64_t currentTime = SDL_GetTicksNS();
    deltaTime = (currentTime - lastTime) / 1000000000.0;
    lastTime = currentTime;
    // 3. УПРАВЛЕНИЕ КАМЕРОЙ
    const bool *keys = SDL_GetKeyboardState(nullptr);
    float currentSpeed = cameraSpeedBase * (float)deltaTime;

    if (keys[SDL_SCANCODE_W])
      cameraPos += currentSpeed * cameraFront;
    if (keys[SDL_SCANCODE_S])
      cameraPos -= currentSpeed * cameraFront;
    if (keys[SDL_SCANCODE_A])
      cameraPos -=
          glm::normalize(glm::cross(cameraFront, cameraUp)) * currentSpeed;
    if (keys[SDL_SCANCODE_D])
      cameraPos +=
          glm::normalize(glm::cross(cameraFront, cameraUp)) * currentSpeed;
    if (keys[SDL_SCANCODE_E])
      cameraPos += currentSpeed * cameraUp;
    if (keys[SDL_SCANCODE_Q])
      cameraPos -= currentSpeed * cameraUp;

    SDL_GPUCommandBuffer *commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUTexture *swapchainTexture;

    Uint32 width, height;
    SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window, &swapchainTexture, &width, &height);

    if (swapchainTexture == NULL) {
      SDL_SubmitGPUCommandBuffer(commandBuffer);
      return SDL_APP_CONTINUE;
    }

    SDL_GPUColorTargetInfo colorTargetInfo{};
    colorTargetInfo.clear_color = {240 / 255.0f, 240 / 255.0f, 240 / 255.0f, 255 / 255.0f};
    colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;
    colorTargetInfo.texture = swapchainTexture;

    SDL_GPUDepthStencilTargetInfo depthTargetInfo{};
    depthTargetInfo.texture = depthTexture;
    depthTargetInfo.clear_depth = 1.0f;
    depthTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    depthTargetInfo.store_op = SDL_GPU_STOREOP_STORE;
    depthTargetInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthTargetInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    UniformBufferVertex uVertexBuffer{};
    glm::mat4 model =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f));
    model = model * glm::rotate(glm::mat4(1.0f),
        // SDL_GetTicks() / 1000.0f,
        0.0f,
                                glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 proj = glm::perspective(
        glm::radians(70.0f), static_cast<float>(width) / height, 0.1f, 100.0f);
    uVertexBuffer.mvp = proj * view * model;

    SDL_PushGPUVertexUniformData(commandBuffer, 0, &uVertexBuffer,
                                 sizeof(UniformBufferVertex));

    // begin a render pass
    SDL_GPURenderPass *renderPass =
        SDL_BeginGPURenderPass(commandBuffer, &colorTargetInfo, 1, &depthTargetInfo);

    SDL_BindGPUGraphicsPipeline(renderPass, pipeline);

    SDL_GPUTextureSamplerBinding tsb{.texture = gpuTexture,
                                     .sampler = gpuSampler};
    SDL_BindGPUFragmentSamplers(renderPass, 0, &tsb, 1);

    SDL_GPUBufferBinding vertexBinding{vertexBuffer, 0};
    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBinding, 1);

    SDL_GPUBufferBinding indexBinding{indexBuffer, 0};
    SDL_BindGPUIndexBuffer(renderPass, &indexBinding,
                           SDL_GPU_INDEXELEMENTSIZE_16BIT);

    SDL_DrawGPUIndexedPrimitives(renderPass, meshIndices.size(), 1, 0, 0, 0);

    // end the render pass
    SDL_EndGPURenderPass(renderPass);

    // submit the command buffer
    SDL_SubmitGPUCommandBuffer(commandBuffer);
  }

  SDL_ReleaseGPUSampler(device, gpuSampler);
  SDL_ReleaseGPUTexture(device, gpuTexture);

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
