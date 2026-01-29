cmake_minimum_required(VERSION 3.31)
project(MyPhysicsEngine)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 1. Описываем зависимости
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

# Для stb просто указываем репозиторий (там много файлов, нам нужен stb_image.h)
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        master
)

# 2. Выкачиваем всё
FetchContent_MakeAvailable(glm SDL3 SDL3_image stb)

# 3. Создаем таргет для stb (так как в самом репо нет CMake)
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

# 4. Основной проект
add_executable(MyGame main.cpp ${SOURCES})

target_link_libraries(MyGame PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    glm::glm
    stb_image
)
