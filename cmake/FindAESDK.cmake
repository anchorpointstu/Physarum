# Locates the After Effects SDK (never committed; see README) and builds the few
# SDK utility sources the plugin needs into a static library.

set(AE_SDK_ROOT "$ENV{AE_SDK_ROOT}" CACHE PATH "After Effects SDK root (folder containing Examples)")

set(_ae_examples "")
if(EXISTS "${AE_SDK_ROOT}/Examples/Headers/AE_Effect.h")
    set(_ae_examples "${AE_SDK_ROOT}/Examples")
elseif(EXISTS "${AE_SDK_ROOT}/Headers/AE_Effect.h")
    set(_ae_examples "${AE_SDK_ROOT}")
endif()
if(NOT _ae_examples)
    message(FATAL_ERROR
        "AE_SDK_ROOT is not set or invalid ('${AE_SDK_ROOT}').\n"
        "Pass -DAE_SDK_ROOT=<path to AfterEffectsSDK_26.5_win> or set the AE_SDK_ROOT environment variable.\n"
        "Expected <root>\\Examples\\Headers\\AE_Effect.h. Use -DPHYSARUM_BUILD_PLUGIN=OFF to build only the CLI.")
endif()

set(AE_SDK_EXAMPLES "${_ae_examples}")
set(AE_SDK_HEADERS "${_ae_examples}/Headers")
set(AE_SDK_UTIL "${_ae_examples}/Util")
set(AE_SDK_RESOURCES "${_ae_examples}/Resources")
set(AE_PIPLTOOL "${AE_SDK_RESOURCES}/PiPLtool.exe")
if(NOT EXISTS "${AE_PIPLTOOL}")
    message(FATAL_ERROR "PiPLtool.exe not found at ${AE_PIPLTOOL}")
endif()
message(STATUS "After Effects SDK: ${AE_SDK_EXAMPLES}")

# Adobe's utility code is compiled without our warnings-as-errors.
add_library(ae_sdk_util STATIC
    "${AE_SDK_UTIL}/AEGP_SuiteHandler.cpp"
    "${AE_SDK_UTIL}/MissingSuiteError.cpp")
target_include_directories(ae_sdk_util PUBLIC
    "${AE_SDK_HEADERS}"
    "${AE_SDK_HEADERS}/SP"
    "${AE_SDK_UTIL}"
    "${AE_SDK_RESOURCES}")
target_compile_definitions(ae_sdk_util PUBLIC MSWindows WIN32 _WINDOWS)
target_compile_options(ae_sdk_util PRIVATE /W3 /EHsc /utf-8)
