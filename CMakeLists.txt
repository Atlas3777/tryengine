cmake_minimum_required(VERSION 3.31)
project(MyPhysicsEngine)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/external")

# Симлинк для LSP (clangd)
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/compile_commands.json")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
    )
endif()

include(FetchContent)

# --- Настройки перед объявлением зависимостей ---
# Отключаем лишнее в SDL
set(SDL3_TESTS OFF CACHE INTERNAL "")
set(SDL3_EXAMPLES OFF CACHE INTERNAL "")
set(SDL3_IMAGE_TESTS OFF CACHE INTERNAL "")

# Отключаем лишнее в Assimp (чтобы не собирать лишние форматы и тесты)
set(ASSIMP_BUILD_TESTS OFF CACHE INTERNAL "")
set(ASSIMP_INSTALL OFF CACHE INTERNAL "")
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE INTERNAL "")
# Можно также отключить ненужные форматы для скорости, например:
# set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE INTERNAL "")
# set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE INTERNAL "")
# set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE INTERNAL "")

# --- Описание зависимостей ---

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        master
)

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        main
)

FetchContent_Declare(
  SDL3_image
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
  GIT_TAG        main
)

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        master
)

# Добавляем Assimp
FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG        v5.4.3 # Рекомендую использовать конкретный тег версии вместо master
)

# --- Выкачиваем и подключаем ---
FetchContent_MakeAvailable(glm SDL3 SDL3_image stb assimp)

# Таргет для stb
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

# --- Основной проект ---
add_executable(tryengine main.cpp)

target_link_libraries(tryengine PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    glm::glm
    stb_image
    assimp::assimp
)
