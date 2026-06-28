#editor/CMakeLists.txt

include(FetchContent)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)

FetchContent_Declare(imgui     GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.6-docking)
FetchContent_Declare(imguizmo  GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git GIT_TAG master)
FetchContent_Declare(stb       GIT_REPOSITORY https://github.com/nothings/stb.git    GIT_TAG master)
FetchContent_Declare(tinygltf  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git  GIT_TAG v3.0.0)

FetchContent_MakeAvailable(imgui imguizmo stb tinygltf)

set(IMGUIZMO_CPP "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
file(READ "${IMGUIZMO_CPP}" IMGUIZMO_CONTENT)
string(REPLACE "static const float quadMin = 0.5f;" "static const float quadMin = 0.3f;" IMGUIZMO_CONTENT "${IMGUIZMO_CONTENT}")
string(REPLACE "static const float quadMax = 0.8f;" "static const float quadMax = 1.f;" IMGUIZMO_CONTENT "${IMGUIZMO_CONTENT}")
string(REPLACE "float mAxisLimit=0.0025f;" "float mAxisLimit=0.0000001f;" IMGUIZMO_CONTENT "${IMGUIZMO_CONTENT}")
string(REPLACE "float mPlaneLimit=0.02f;" "float mPlaneLimit=0.05f;" IMGUIZMO_CONTENT "${IMGUIZMO_CONTENT}")
file(WRITE "${IMGUIZMO_CPP}" "${IMGUIZMO_CONTENT}")

add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)

target_compile_definitions(imgui_lib PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)

target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PRIVATE SDL3::SDL3)

target_link_libraries(imguizmo PRIVATE imgui_lib)


file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/editor/*.hpp")

add_executable(editor ${SOURCES} ${HEADERS})
target_include_directories(editor PRIVATE include src)

target_link_libraries(editor PRIVATE
        engine_core
        engine_resources
        engine_graphics
        imgui_lib
        imguizmo        
        ${CMAKE_DL_LIBS}
        tinygltf
        stb_image
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/EditorScriptBindings.cpp
            PROPERTIES COMPILE_OPTIONS "-fno-rtti"
    )
endif()