include(FetchContent)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

set(TINYGLTF3_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINYGLTF3_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(imgui        GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.6-docking GIT_SHALLOW TRUE)
FetchContent_Declare(stb          GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_Declare(tinygltf     GIT_REPOSITORY https://github.com/syoyo/tinygltf.git  GIT_TAG v3.0.1 GIT_SHALLOW TRUE)
FetchContent_Declare(bc7enc       GIT_REPOSITORY https://github.com/richgel999/bc7enc_rdo.git GIT_TAG master GIT_SHALLOW TRUE SOURCE_SUBDIR "DO_NOT_BUILD")

FetchContent_Declare(slang
        DOWNLOAD_ONLY
        URL "https://github.com/shader-slang/slang/releases/download/v2026.14.1/slang-2026.14.1-linux-x86_64-glibc-2.28.tar.gz"
        URL_HASH SHA256=07cf4274f5f1f976d75554cc5c1cc4bd993a9a92442a7726fa701aa457183a26
)

FetchContent_MakeAvailable(imgui stb tinygltf bc7enc slang)

add_library(slang_lib SHARED IMPORTED GLOBAL)
set_target_properties(slang_lib PROPERTIES
        IMPORTED_LOCATION "${slang_SOURCE_DIR}/lib/libslang.so"
        INTERFACE_INCLUDE_DIRECTORIES "${slang_SOURCE_DIR}/include"
)

add_library(tinygltf STATIC
        "${tinygltf_SOURCE_DIR}/tiny_gltf_v3.c"
)
target_include_directories(tinygltf PUBLIC "${tinygltf_SOURCE_DIR}")

add_library(stb_image INTERFACE)
target_include_directories(stb_image SYSTEM INTERFACE ${stb_SOURCE_DIR})

add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

set_target_properties(imgui_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(imgui_lib PUBLIC
        IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1
        VK_NO_PROTOTYPES
        IMGUI_IMPL_VULKAN_USE_VOLK
)

target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PRIVATE SDL3::SDL3 volk)

add_library(bc7enc_lib STATIC
        ${bc7enc_SOURCE_DIR}/bc7enc.cpp
        ${bc7enc_SOURCE_DIR}/bc7decomp.cpp
        ${bc7enc_SOURCE_DIR}/rgbcx.cpp
        ${bc7enc_SOURCE_DIR}/ert.cpp
        ${bc7enc_SOURCE_DIR}/rdo_bc_encoder.cpp
        ${bc7enc_SOURCE_DIR}/utils.cpp
        ${bc7enc_SOURCE_DIR}/bc7decomp_ref.cpp
        ${bc7enc_SOURCE_DIR}/lodepng.cpp
)

target_include_directories(bc7enc_lib PUBLIC ${bc7enc_SOURCE_DIR})
target_compile_options(bc7enc_lib PRIVATE "-include" "cstdint")
target_compile_options(bc7enc_lib PRIVATE "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-strict-aliasing>")

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(bc7enc_lib PUBLIC OpenMP::OpenMP_CXX)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/editor/*.hpp")

list(REMOVE_ITEM SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SHADERC REQUIRED IMPORTED_TARGET shaderc)

add_library(editor_core STATIC ${SOURCES} ${HEADERS})

set_target_properties(editor_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(editor_core PUBLIC include src)

target_link_libraries(editor_core PUBLIC
        engine::core
        engine::resources
        engine::graphics
        engine::async
        engine::platform
        imgui_lib
        tinygltf
        stb_image
        bc7enc_lib
        slang_lib
        PkgConfig::SHADERC
)

add_executable(editor src/main.cpp)
target_link_libraries(editor PRIVATE editor_core)

add_library(tryeditor_shared SHARED src/Module_TryEditor.cpp src/Module_Resources.cpp)
target_link_libraries(tryeditor_shared PRIVATE editor_core)

set_target_properties(tryeditor_shared PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/modules/tryeditor"
        SUFFIX ".shared_module"
        PREFIX ""
        OUTPUT_NAME "tryeditor"
)

# Указываем линкеру, где искать libslang.so при запуске бинарника и подгрузке .shared_module
set_target_properties(editor tryeditor_shared PROPERTIES
        BUILD_RPATH "${slang_SOURCE_DIR}/lib"
)