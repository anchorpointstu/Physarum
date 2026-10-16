# cmake -P helper: runs "cl /nologo /EP" on a file and writes stdout to a file.
# Replaces the shell redirection used by the SDK's PiPL build steps.
#   -DCL=<cl.exe> -DIN=<file> -DOUT=<file> [-DINCLUDES=a|b] [-DDEFINES=X|Y]

set(args /nologo /EP)
if(INCLUDES)
    string(REPLACE "|" ";" _incs "${INCLUDES}")
    foreach(i IN LISTS _incs)
        list(APPEND args "/I${i}")
    endforeach()
endif()
if(DEFINES)
    string(REPLACE "|" ";" _defs "${DEFINES}")
    foreach(d IN LISTS _defs)
        list(APPEND args "/D${d}")
    endforeach()
endif()

execute_process(
    COMMAND "${CL}" ${args} "${IN}"
    OUTPUT_FILE "${OUT}"
    ERROR_VARIABLE err
    RESULT_VARIABLE rv)
if(NOT rv EQUAL 0)
    message(FATAL_ERROR "Preprocessing ${IN} failed (${rv}):\n${err}")
endif()
