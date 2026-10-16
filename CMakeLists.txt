cmake_minimum_required(VERSION 3.25)

project(Physarum VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Physarum builds for 64-bit Windows only.")
endif()

option(PHYSARUM_BUILD_PLUGIN "Build the After Effects plugin (needs AE_SDK_ROOT)" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the .aex has no VC runtime dependency on the user's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Debug info embedded in objects (/Z7) avoids PDB contention under Ninja.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")

# Flat output (build\<Config>\Physarum.aex). The genex stops multi-config generators
# from appending a per-configuration folder; build.ps1 uses one build dir per config.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}>)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Shaders)

set(PHYSARUM_GEN_DIR ${CMAKE_BINARY_DIR}/generated)

# Flags for our own code. SDK code gets a lighter set (see FindAESDK.cmake).
function(physarum_project_flags target)
    target_compile_options(${target} PRIVATE
        /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus
        $<$<CONFIG:Release>:/O2 /Ob2 /Gy>)
    target_compile_definitions(${target} PRIVATE
        MSWindows WIN32 _WINDOWS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
        $<$<CONFIG:Debug>:PHYSARUM_DEBUG=1>)
endfunction()

function(physarum_link_flags target)
    target_link_options(${target} PRIVATE /DEBUG $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
endfunction()

# ---------------------------------------------------------------- shaders
set(PHYSARUM_SHADERS
    cs_clear_uint
    cs_spawn
    cs_agent_step
    cs_trail_resolve
    cs_trail_diffuse
    cs_agents_draw
    cs_present)

set(PHYSARUM_SHADER_HEADERS)
foreach(s IN LISTS PHYSARUM_SHADERS)
    physarum_compile_shader(${s} PHYSARUM_SHADER_HEADERS)
endforeach()
add_custom_target(physarum_shaders DEPENDS ${PHYSARUM_SHADER_HEADERS})

# ---------------------------------------------------------------- simulation core
add_library(physarum_sim STATIC
    src/sim/SimParams.h
    src/sim/SimInstance.h
    src/sim/SimInstance.cpp
    src/sim/Presets.h
    src/sim/Presets.cpp
    src/sim/dx12/Dx12Common.h
    src/sim/dx12/Dx12Common.cpp
    src/sim/dx12/Dx12Buffer.h
    src/sim/dx12/Dx12Context.h
    src/sim/dx12/Dx12Context.cpp
    src/sim/dx12/ShaderIds.h
    src/sim/dx12/ShaderIds.cpp)
add_dependencies(physarum_sim physarum_shaders)
target_include_directories(physarum_sim
    PUBLIC  ${CMAKE_SOURCE_DIR}/src
    PRIVATE ${PHYSARUM_GEN_DIR})
target_link_libraries(physarum_sim PUBLIC d3d12 dxgi)
physarum_project_flags(physarum_sim)

# ---------------------------------------------------------------- CLI test harness
add_executable(physarum_cli
    src/cli/main.cpp
    src/cli/Args.h
    src/cli/Args.cpp
    src/cli/ImageWrite.h
    src/cli/ImageWrite.cpp)
target_link_libraries(physarum_cli PRIVATE physarum_sim)
physarum_project_flags(physarum_cli)
physarum_link_flags(physarum_cli)

# ---------------------------------------------------------------- After Effects plugin
if(PHYSARUM_BUILD_PLUGIN AND EXISTS ${CMAKE_SOURCE_DIR}/src/ae/Physarum.cpp)
    include(FindAESDK)
    include(Plugin)
endif()
