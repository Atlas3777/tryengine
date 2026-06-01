# engine_source/CMakeLists.txt
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

set(DAS_GLFW_DISABLED TRUE CACHE BOOL "" FORCE)
set(DAS_AUDIO_DISABLED TRUE CACHE BOOL "" FORCE)
set(DAS_HV_DISABLED TRUE CACHE BOOL "" FORCE)
set(DAS_STBIMAGE_DISABLED TRUE CACHE BOOL "" FORCE)
set(DAS_MINFFT_DISABLED TRUE CACHE BOOL "" FORCE)
set(DAS_PUGIXML_DISABLED TRUE CACHE BOOL "" FORCE)

set(DAS_OPENGL_DISABLED TRUE CACHE BOOL "" FORCE)
set(DAS_GLSL_DISABLED TRUE CACHE BOOL "" FORCE)
set(DAS_STD_DLG_DISABLED TRUE CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3     GIT_REPOSITORY https://github.com/libsdl-org/SDL.git  GIT_TAG main)
FetchContent_Declare(entt     GIT_REPOSITORY https://github.com/skypjack/entt.git   GIT_TAG v3.16.0)
FetchContent_Declare(glm      GIT_REPOSITORY https://github.com/g-truc/glm.git      GIT_TAG master)
FetchContent_Declare(cereal   GIT_REPOSITORY https://github.com/USCiLab/cereal.git  GIT_TAG v1.3.2)
FetchContent_Declare(daslang  GIT_REPOSITORY https://github.com/GaijinEntertainment/daScript.git GIT_TAG master)

FetchContent_MakeAvailable(SDL3 entt glm cereal daslang)

# 2. МЫ ТУТ: Создаем симлинк в engine_content прямо в корне проекта
set(DASLIB_SOURCE "${daslang_SOURCE_DIR}/daslib")
set(DASLIB_TARGET "${CMAKE_SOURCE_DIR}/engine_content/daslib")

if(NOT EXISTS "${DASLIB_TARGET}")
    message(STATUS "Creating symlink for daslib into engine_content...")
    if(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
        file(COPY "${DASLIB_SOURCE}" DESTINATION "${CMAKE_SOURCE_DIR}/engine_content")
    else()
        file(CREATE_LINK "${DASLIB_SOURCE}" "${DASLIB_TARGET}" SYMBOLIC)
    endif()
endif()

add_subdirectory(core)
add_subdirectory(resources)
add_subdirectory(graphics)