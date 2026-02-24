cmake_minimum_required(VERSION 3.31)
project(MyPhysicsEngine)

# --- 1. Базовые настройки проекта ---

# Тип сборки по умолчанию (Оптимизированный, но с отладочной информацией)
# Важно для физического движка, чтобы он не тормозил при разработке
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build" FORCE)
endif()

# Настройка кэша компилятора (ccache) для ускорения пересборки
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Строгий стандарт C++23 без компиляторных расширений (gnu++23 -> c++23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) 
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# --- 2. Настройка среды разработки ---

set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/external")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Генерация симлинка compile_commands.json для автодополнения в IDE (VS Code, CLion, Neovim)
set(SRC_LINK "${CMAKE_SOURCE_DIR}/compile_commands.json")
set(BIN_FILE "${CMAKE_BINARY_DIR}/compile_commands.json")

if(UNIX)
    execute_process(COMMAND ${CMAKE_COMMAND} -E rm -f "${SRC_LINK}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${BIN_FILE}"
        "${SRC_LINK}"
    )
endif()

# --- 3. Конфигурация зависимостей перед скачиванием ---

include(FetchContent)

# SDL3: отключаем тесты и примеры
set(SDL3_TESTS OFF CACHE INTERNAL "")
set(SDL3_EXAMPLES OFF CACHE INTERNAL "")

# Assimp: максимальная оптимизация, оставляем ТОЛЬКО glTF
# set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE INTERNAL "")
# set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE INTERNAL "")
# 
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE INTERNAL "")
set(ASSIMP_INSTALL OFF CACHE INTERNAL "")
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE INTERNAL "")

# --- 4. Объявление зависимостей (Библиотеки) ---

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        master
)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        main
)

# FetchContent_Declare(SDL3_image
#     GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
#     GIT_TAG        main
# )

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)

FetchContent_Declare(entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.16.0
)

# FetchContent_Declare(assimp
#     GIT_REPOSITORY https://github.com/assimp/assimp.git
#     GIT_TAG        v5.4.3
# )

FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        release # или конкретный тег, например v2.9.3
)


# --- ImGui (ветка docking) ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)



set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)

FetchContent_MakeAvailable(glm SDL3 stb entt tinygltf imgui)# Выкачиваем и подключаем всё объявленное выше

# --- 5. Настройка таргетов и сборки движка ---


# Умный поиск исходников (CONFIGURE_DEPENDS заставит CMake реагировать на новые файлы)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.hpp"
)

add_executable(tryengine ${SOURCES})

# Теперь создаем библиотеку, используя путь, который подготовил FetchContent
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    # Бэккенды для SDL3 и SDL_GPU
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)

# Настройка путей и зависимостей
target_include_directories(imgui PUBLIC 
    ${imgui_SOURCE_DIR} 
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PRIVATE SDL3::SDL3)
# Указываем пути к заголовкам
target_include_directories(tryengine PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_library(stb_image INTERFACE)
target_include_directories(stb_image SYSTEM INTERFACE ${stb_SOURCE_DIR})

add_library(tinygltf_lib INTERFACE)
target_include_directories(tinygltf_lib SYSTEM INTERFACE ${tinygltf_SOURCE_DIR})

# 2. Внешние заголовки как SYSTEM (это ключевой момент для подавления варнингов)
target_include_directories(tryengine SYSTEM PRIVATE 
    ${glm_SOURCE_DIR}
    ${entt_SOURCE_DIR}/src 
    ${stb_SOURCE_DIR}
)



# Линковка
target_link_libraries(tryengine PRIVATE
    SDL3::SDL3
    glm::glm
    stb_image
    # assimp::assimp
    EnTT::EnTT
    tinygltf_lib
    imgui
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Твои строгие варнинги
    # target_compile_options(tryengine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)

    if(USE_ASAN)
        message(STATUS "ASAN IS ENABLED")
        
        # Перечисляем флаги через точку с запятой (список CMake)
        set(ASAN_FLAGS "-fsanitize=address" "-fno-omit-frame-pointer" "-fsanitize=undefined")
        
        target_compile_options(tryengine PRIVATE ${ASAN_FLAGS})
        target_link_options(tryengine PRIVATE ${ASAN_FLAGS})
        
        # target_compile_options(imgui PRIVATE ${ASAN_FLAGS})
        # target_link_options(imgui PRIVATE ${ASAN_FLAGS})
    endif()
endif()
