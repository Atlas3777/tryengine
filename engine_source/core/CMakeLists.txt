set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)

set(EABASE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(EASTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EASTL_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)

CPMAddPackage(
        NAME EABase
        URL https://github.com/electronicarts/EABase/archive/0699a15efdfd20b6cecf02153bfa5663decb653c.zip
        DOWNLOAD_ONLY YES
)

CPMAddPackage(
        NAME EASTL
        GIT_REPOSITORY https://github.com/electronicarts/EASTL.git
        GIT_TAG e323f67e5d93fdadcd44f7998069d804697b1945
        GIT_SHALLOW TRUE
        SYSTEM YES
)

add_library(EABase INTERFACE)
target_include_directories(EABase SYSTEM INTERFACE "${EABase_SOURCE_DIR}/include/Common")

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