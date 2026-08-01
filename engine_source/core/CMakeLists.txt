# engine_source/core/CMakeLists.txt

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

# Отключаем сборку тестов и бенчмарков для EASTL
set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(EASTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(EASTL_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(entt     GIT_REPOSITORY https://github.com/skypjack/entt.git   GIT_TAG v3.16.0)
FetchContent_Declare(glm      GIT_REPOSITORY https://github.com/g-truc/glm.git      GIT_TAG master)
FetchContent_Declare(EASTL    GIT_REPOSITORY https://github.com/electronicarts/EASTL.git GIT_TAG master SYSTEM)

FetchContent_MakeAvailable(entt glm EASTL)

find_package(DAS REQUIRED)
find_package(Threads REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/core/*.hpp")

add_library(engine_core STATIC ${SOURCES} ${HEADERS})
add_library(engine::core ALIAS engine_core)

set_target_properties(engine_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(engine_core PRIVATE cxx_std_23)

target_include_directories(engine_core PUBLIC include PRIVATE src)
target_link_libraries(engine_core PUBLIC
        glm::glm
        EnTT::EnTT
        EASTL
        DAS::libDaScriptDyn
        Threads::Threads
)

get_target_property(DAS_INC_DIRS DAS::libDaScriptDyn INTERFACE_INCLUDE_DIRECTORIES)
list(GET DAS_INC_DIRS 0 DAS_MAIN_INCLUDE_DIR)
cmake_path(GET DAS_MAIN_INCLUDE_DIR PARENT_PATH DAS_SDK_ROOT)

target_compile_definitions(engine_core PUBLIC
        DAS_MOD_EXPORTS
        "DAS_ROOT_DIR=\"${DAS_SDK_ROOT}\""
)