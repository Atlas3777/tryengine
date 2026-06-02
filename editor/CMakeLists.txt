# ./editor/CMakeLists.txt

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

FetchContent_Declare(imgui     GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG docking)
FetchContent_Declare(imguizmo  GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git GIT_TAG master)
FetchContent_Declare(stb       GIT_REPOSITORY https://github.com/nothings/stb.git    GIT_TAG master)
FetchContent_Declare(tinygltf  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git  GIT_TAG v3.0.0)

FetchContent_MakeAvailable(imgui imguizmo stb tinygltf)

# --- ПАТЧИНГ IMGUIZMO ---
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
        ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
#
#        # Интегрируем биндинги прямо в нашу imgui библиотеку
#        ${DASIMGUI_SRCS}
)

target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imguizmo_SOURCE_DIR}/src

#        # Пути к заголовкам биндингов и самого daScript
#        ${DASIMGUI_ROOT_DIR}/src
#        ${DASLANG_DIR}/include
)

target_link_libraries(imgui_lib PRIVATE SDL3::SDL3)


file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/editor/*.hpp")

add_executable(editor ${SOURCES} ${HEADERS})

target_include_directories(editor PRIVATE include src)

target_link_libraries(editor PRIVATE
        engine_core
        engine_resources
        engine_graphics
        imgui_lib
        ${CMAKE_DL_LIBS}

        tinygltf
        stb_image
)