#engine_source/resources/CMakeLists.txt

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

set(JUST_INSTALL_CEREAL ON CACHE BOOL "" FORCE)

FetchContent_Declare(cereal   GIT_REPOSITORY https://github.com/USCiLab/cereal.git  GIT_TAG v1.3.2)
FetchContent_MakeAvailable(cereal)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/resources/*.hpp")

if(NOT TARGET stb_image)
    add_library(stb_image INTERFACE)
    target_include_directories(stb_image SYSTEM INTERFACE ${stb_SOURCE_DIR})
endif()

add_library(engine_resources STATIC ${SOURCES} ${HEADERS})

target_include_directories(engine_resources PUBLIC include PRIVATE src)

target_link_libraries(engine_resources
        PUBLIC engine_core
)
