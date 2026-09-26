set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

set(SDL3_TESTS OFF CACHE INTERNAL "")
set(SDL3_EXAMPLES OFF CACHE INTERNAL "")
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_GPU OFF CACHE BOOL "" FORCE)
set(SDL_GPU_OPENXR OFF CACHE BOOL "" FORCE)

include(FetchContent)

# Единый источник актуальных заголовков Vulkan SDK (1.4+)
FetchContent_Declare(
        Vulkan-Headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG main
        GIT_SHALLOW TRUE
)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG main GIT_SHALLOW TRUE)
FetchContent_Declare(hlsl GIT_REPOSITORY https://github.com/redorav/hlslpp.git GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_Declare(volk GIT_REPOSITORY https://github.com/zeux/volk.git GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_Declare(vk-bootstrap GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git GIT_TAG main GIT_SHALLOW TRUE)

FetchContent_Declare(
        vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(Vulkan-Headers SDL3 hlsl volk vk-bootstrap vma)

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