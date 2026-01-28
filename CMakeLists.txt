cmake_minimum_required(VERSION 3.31)
project(MyPhysicsEngine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL_image EXCLUDE_FROM_ALL)

# 5. Создаем исполняемый файл
add_executable(MyGame ${SOURCES} main.cpp)

# 4. Линовка библиотек
# SDL3::SDL3 — это целевое имя, которое создает подпроект SDL
target_link_libraries(MyGame PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
)
