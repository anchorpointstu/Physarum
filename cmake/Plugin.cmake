# The After Effects plugin target (Physarum.aex) and its PiPL resource.

set(_pipl_dir "${CMAKE_BINARY_DIR}/generated/pipl")
set(_pipl_src "${CMAKE_SOURCE_DIR}/src/ae/PhysarumPiPL.r")
set(_pipl_rr "${_pipl_dir}/PhysarumPiPL.rr")
set(_pipl_rrc "${_pipl_dir}/PhysarumPiPL.rrc")
set(_pipl_rc "${_pipl_dir}/PhysarumPiPL.rc")

# Same three steps as the SDK's Skeleton.vcxproj: preprocess the .r, run PiPLtool,
# preprocess again with MSWindows defined. The .rc is then compiled into the DLL.
add_custom_command(
    OUTPUT "${_pipl_rc}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_pipl_dir}"
    COMMAND ${CMAKE_COMMAND} "-DCL=${CMAKE_CXX_COMPILER}" "-DIN=${_pipl_src}" "-DOUT=${_pipl_rr}"
            "-DINCLUDES=${AE_SDK_HEADERS}|${CMAKE_SOURCE_DIR}/src/common"
            -P "${CMAKE_SOURCE_DIR}/cmake/Preprocess.cmake"
    COMMAND "${AE_PIPLTOOL}" "${_pipl_rr}" "${_pipl_rrc}"
    COMMAND ${CMAKE_COMMAND} "-DCL=${CMAKE_CXX_COMPILER}" "-DIN=${_pipl_rrc}" "-DOUT=${_pipl_rc}"
            "-DDEFINES=MSWindows"
            -P "${CMAKE_SOURCE_DIR}/cmake/Preprocess.cmake"
    DEPENDS "${_pipl_src}" "${CMAKE_SOURCE_DIR}/src/common/PhysarumVersion.h" "${AE_PIPLTOOL}"
            "${CMAKE_SOURCE_DIR}/cmake/Preprocess.cmake"
    COMMENT "Building PiPL resource"
    VERBATIM)

add_library(Physarum MODULE
    src/common/PhysarumVersion.h
    src/ae/Physarum.h
    src/ae/Physarum.cpp
    src/ae/Params.h
    src/ae/Params.cpp
    src/ae/Registry.h
    src/ae/Registry.cpp
    src/ae/Render.cpp
    src/ae/PhysarumPiPL.r
    "${_pipl_rc}")
set_source_files_properties(src/ae/PhysarumPiPL.r PROPERTIES HEADER_FILE_ONLY TRUE)
set_target_properties(Physarum PROPERTIES PREFIX "" SUFFIX ".aex")
target_include_directories(Physarum PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/common")
target_link_libraries(Physarum PRIVATE physarum_sim ae_sdk_util)
physarum_project_flags(Physarum)
physarum_link_flags(Physarum)
