# engine/core/CMakeLists.txt

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/core/*.hpp")

add_library(engine_core STATIC ${SOURCES} ${HEADERS})

target_include_directories(engine_core PUBLIC include PRIVATE src)

target_link_libraries(engine_core PUBLIC
        glm::glm
        EnTT::EnTT
        cereal::cereal
        DAS::libDaScriptDyn
        Threads::Threads  # Потоки (обязательно для daScript)
)

get_target_property(DAS_INC_DIRS DAS::libDaScript INTERFACE_INCLUDE_DIRECTORIES)

list(GET DAS_INC_DIRS 0 DAS_MAIN_INCLUDE_DIR)

cmake_path(GET DAS_MAIN_INCLUDE_DIR PARENT_PATH DAS_SDK_ROOT)

target_compile_definitions(engine_core PUBLIC
        DAS_MOD_EXPORTS
        "DAS_ROOT_DIR=\"${DAS_SDK_ROOT}\""
)