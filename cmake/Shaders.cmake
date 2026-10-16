# Compiles HLSL compute shaders at build time into C headers that are embedded
# in the binaries, so the plugin ships as a single .aex with no loose shader files.

set(PHYSARUM_SHADER_COMPILER "fxc" CACHE STRING "HLSL compiler: fxc (cs_5_1, DXBC) or dxc (cs_6_0, DXIL)")
set_property(CACHE PHYSARUM_SHADER_COMPILER PROPERTY STRINGS fxc dxc)

set(_kits_bin_hints
    "$ENV{WindowsSdkVerBinPath}x64"
    "$ENV{WindowsSdkDir}bin/$ENV{WindowsSDKVersion}x64"
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64")

if(PHYSARUM_SHADER_COMPILER STREQUAL "dxc")
    find_program(PHYSARUM_DXC_EXE dxc HINTS ${_kits_bin_hints} REQUIRED)
else()
    find_program(PHYSARUM_FXC_EXE fxc HINTS ${_kits_bin_hints} REQUIRED)
endif()

set(PHYSARUM_SHADER_DIR ${CMAKE_SOURCE_DIR}/src/shaders)
file(GLOB PHYSARUM_SHADER_INCLUDES CONFIGURE_DEPENDS ${PHYSARUM_SHADER_DIR}/*.hlsli)

# physarum_compile_shader(<name> <out_list_var>)
# Same flags for Debug and Release so both builds produce identical images.
function(physarum_compile_shader name out_list)
    set(src ${PHYSARUM_SHADER_DIR}/${name}.hlsl)
    set(out_dir ${CMAKE_BINARY_DIR}/generated/shaders)
    set(out ${out_dir}/${name}.h)
    if(PHYSARUM_SHADER_COMPILER STREQUAL "dxc")
        set(cmd ${PHYSARUM_DXC_EXE} -nologo -T cs_6_0 -E main -O3 -WX -I ${PHYSARUM_SHADER_DIR}
                -Fh ${out} -Vn g_${name} ${src})
    else()
        set(cmd ${PHYSARUM_FXC_EXE} /nologo /T cs_5_1 /E main /O3 /WX /Ges /Qstrip_reflect /Qstrip_debug
                /I ${PHYSARUM_SHADER_DIR} /Fh ${out} /Vn g_${name} ${src})
    endif()
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
        COMMAND ${cmd}
        DEPENDS ${src} ${PHYSARUM_SHADER_INCLUDES}
        COMMENT "Compiling shader ${name}.hlsl"
        VERBATIM)
    set(${out_list} ${${out_list}} ${out} PARENT_SCOPE)
endfunction()
