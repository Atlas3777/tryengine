# engine/CMakeLists.txt
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

set(SDL3_TESTS OFF CACHE INTERNAL "")
set(SDL3_EXAMPLES OFF CACHE INTERNAL "")
set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(JUST_INSTALL_CEREAL ON CACHE BOOL "" FORCE)

FetchContent_Declare(glm      GIT_REPOSITORY https://github.com/g-truc/glm.git      GIT_TAG master)
FetchContent_Declare(SDL3     GIT_REPOSITORY https://github.com/libsdl-org/SDL.git  GIT_TAG main)
FetchContent_Declare(stb      GIT_REPOSITORY https://github.com/nothings/stb.git    GIT_TAG master)
FetchContent_Declare(entt     GIT_REPOSITORY https://github.com/skypjack/entt.git   GIT_TAG v3.16.0)
FetchContent_Declare(tinygltf GIT_REPOSITORY https://github.com/syoyo/tinygltf.git  GIT_TAG release)
FetchContent_Declare(cereal   GIT_REPOSITORY https://github.com/USCiLab/cereal.git  GIT_TAG v1.3.2)

FetchContent_MakeAvailable(glm SDL3 stb entt tinygltf cereal)

add_subdirectory(core)
add_subdirectory(resources)
add_subdirectory(graphics)