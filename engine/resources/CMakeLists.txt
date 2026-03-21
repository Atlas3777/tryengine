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
        PRIVATE tinygltf stb_image
)
