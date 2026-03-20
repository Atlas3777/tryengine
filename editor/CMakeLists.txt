# editor/CMakeLists.txt
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

FetchContent_Declare(imgui    GIT_REPOSITORY https://github.com/ocornut/imgui.git    GIT_TAG docking)
FetchContent_Declare(imguizmo GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git GIT_TAG master)
FetchContent_MakeAvailable(imgui imguizmo)

# ==================== ImGui ====================
add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
        ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
)

target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imguizmo_SOURCE_DIR}
)
target_link_libraries(imgui_lib PRIVATE SDL3::SDL3)

# ==================== Editor Executable ====================
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/editor/*.hpp")

# Делаем сразу executable, избавляясь от лишней прослойки
add_executable(editor ${SOURCES} ${HEADERS} )

target_include_directories(editor PRIVATE
        include
        src
        ../game/include
)

target_link_libraries(editor
        PRIVATE
        engine
        imgui_lib
        ${CMAKE_DL_LIBS}
)

# Оставляем зависимость, чтобы при сборке проекта сначала собиралась игра,
# но НЕ ЛИНКУЕМ её бинарно.
if(TARGET game_shared)
    add_dependencies(editor game_shared)

    # Твой POST_BUILD остается — он просто копирует файл рядом
    add_custom_command(TARGET editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:game_shared>
            $<TARGET_FILE_DIR:editor>
            COMMENT "Copying game shared library → editor output dir"
    )
endif()