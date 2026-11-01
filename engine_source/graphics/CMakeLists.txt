set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

# ==========================================
# Настройки SDL3 (минимальный билд под Vulkan)
# ==========================================

# Базовые подсистемы (оставляем только VIDEO для окна и инпута)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_VIDEO ON CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_NOTIFICATION OFF CACHE BOOL "" FORCE)

# Графика (только Vulkan, убираем OpenGL/GLES/Metal и внутренний 2D рендер)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_METAL OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_VULKAN OFF CACHE BOOL "" FORCE) # SDL рендер не нужен, ImGui рендерит сам

# Отключаем специфичные тяжелые бекенды (Linux/Windows)
set(SDL_DBUS OFF CACHE BOOL "" FORCE)
set(SDL_IBUS OFF CACHE BOOL "" FORCE)
set(SDL_LIBUDEV OFF CACHE BOOL "" FORCE)
set(SDL_LIBURING OFF CACHE BOOL "" FORCE)
set(SDL_DIRECTX OFF CACHE BOOL "" FORCE)

# Тесты, примеры и установка в систему
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

# Урезанная сборка (выкидывает часть легаси-кода)
set(SDL_LEAN_AND_MEAN ON CACHE BOOL "" FORCE)

# Билдим ТОЛЬКО статику (чтобы линковать в свой engine и не таскать SDL3.dll/.so)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
# ==========================================

CPMAddPackage(
        NAME Vulkan-Headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG 9fbc2a782ee3f8a4db64e7d991e23da75e893f44
        GIT_SHALLOW TRUE
)

CPMAddPackage(
        NAME SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG bd9f6ca9ff0e9509f49980fc1b70837f54b6f244
        GIT_SHALLOW TRUE
)

CPMAddPackage(
        NAME hlsl
        GIT_REPOSITORY https://github.com/redorav/hlslpp.git
        GIT_TAG 3d9af2e77a89ddd645928f1600a0688dc898dedc
        GIT_SHALLOW TRUE
)

CPMAddPackage(
        NAME volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG aa76c86ae99aa76f7411c981dc885f68a08513b5
        GIT_SHALLOW TRUE
)

CPMAddPackage(
        NAME vk-bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
        GIT_TAG 21724bb5db25dda6e6e7042b5f4272d6cddf8eb5
        GIT_SHALLOW TRUE
)

CPMAddPackage(
        NAME vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG 0f1a757da579831283413295cc7f3b47eb521c77
        GIT_SHALLOW TRUE
)

set_target_properties(vk-bootstrap PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(volk PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(hlsl_lib INTERFACE)
target_include_directories(hlsl_lib SYSTEM INTERFACE ${hlsl_SOURCE_DIR}/include)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/graphics/*.hpp")

add_library(engine_graphics STATIC ${SOURCES} ${HEADERS})
add_library(engine::graphics ALIAS engine_graphics)

target_include_directories(engine_graphics PUBLIC include PRIVATE src)

target_precompile_headers(engine_graphics
        PRIVATE
        src/engine_pch.hpp
)

target_link_libraries(engine_graphics PUBLIC
        Vulkan::Headers
        hlsl_lib
        engine::core
        engine::resources
        engine::platform
        SDL3::SDL3
        volk
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
)

target_compile_definitions(engine_graphics PUBLIC VK_NO_PROTOTYPES)

add_library(tryengine_shared SHARED src/Module_Renderer.cpp)
target_link_libraries(tryengine_shared PRIVATE engine::core engine::graphics)

set_target_properties(tryengine_shared PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/modules/tryengine"
        SUFFIX ".shared_module"
        PREFIX ""
        OUTPUT_NAME "tryengine"
)