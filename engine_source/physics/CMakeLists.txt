# Физика: Jolt Physics в отдельном слое.
#
# Jolt — сторонняя библиотека, поэтому он живёт только здесь: editor и game_client
# линкуют engine::physics и не включают заголовки Jolt напрямую. Публичный интерфейс
# слоя — PhysicsWorld.hpp, без Jolt-типов.

set(USE_ASSERTS OFF)
set(DOUBLE_PRECISION OFF)
set(GENERATE_DEBUG_SYMBOLS ON)
set(OVERRIDE_CXX_FLAGS OFF)
set(CROSS_PLATFORM_DETERMINISTIC OFF)
set(CROSS_COMPILE_ARM OFF)
set(JPH_BUILD_SHARED_LIBS OFF)
set(INTERPROCEDURAL_OPTIMIZATION OFF)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)
set(CPP_EXCEPTIONS_ENABLED OFF)
set(CPP_RTTI_ENABLED OFF)
set(OBJECT_LAYER_BITS 16)
set(USE_SSE4_1 OFF)
set(USE_SSE4_2 OFF)
set(USE_AVX OFF)
set(USE_AVX2 OFF)
set(USE_AVX512 OFF)
set(USE_LZCNT OFF)
set(USE_TZCNT OFF)
set(USE_F16C OFF)
set(USE_FMADD OFF)
set(ENABLE_ALL_WARNINGS OFF)
set(TRACK_BROADPHASE_STATS OFF)
set(TRACK_NARROWPHASE_STATS OFF)
set(JPH_TRACK_SIMULATION_STATS OFF)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
set(DEBUG_RENDERER_IN_DISTRIBUTION OFF)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
set(PROFILER_IN_DISTRIBUTION OFF)
set(JPH_USE_EXTERNAL_PROFILE OFF)
set(DISABLE_CUSTOM_ALLOCATOR OFF)
set(ENABLE_OBJECT_STREAM OFF)
set(ENABLE_INSTALL OFF)
set(JPH_USE_DX12 OFF)
set(JPH_USE_VK OFF)
set(JPH_USE_MTL OFF)
set(JPH_USE_CPU_COMPUTE OFF)

CPMAddPackage(
        NAME JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG v5.7.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR Build
)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/engine/physics/*.hpp")

add_library(engine_physics STATIC ${SOURCES} ${HEADERS})
add_library(engine::physics ALIAS engine_physics)

target_compile_features(engine_physics PRIVATE cxx_std_23)
target_include_directories(engine_physics PUBLIC include PRIVATE src)

target_link_libraries(engine_physics PUBLIC
        engine::core
        Jolt::Jolt
)

# Модуль-биндинг daslang: тот же PhysicsWorld из скрипта. Файл лежит в этом слое,
# потому что отдаёт в скрипт типы и состояние физики.
tryengine_add_das_module(tryPhysics_shared tryPhysics src/Module_Physics.cpp engine::physics)
