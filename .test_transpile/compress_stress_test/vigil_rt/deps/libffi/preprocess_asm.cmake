# Preprocess a .S file with the C compiler (/EP) and write to OUTPUT.
# Usage: cmake -DCC=... -DINPUT=... -DOUTPUT=... -DINCLUDES=... -DDEFINES=... -P preprocess_asm.cmake

separate_arguments(INCLUDE_FLAGS NATIVE_COMMAND "${INCLUDES}")
separate_arguments(DEFINE_FLAGS NATIVE_COMMAND "${DEFINES}")

execute_process(
    COMMAND "${CC}" /nologo /EP ${INCLUDE_FLAGS} ${DEFINE_FLAGS} "${INPUT}"
    OUTPUT_FILE "${OUTPUT}"
    RESULT_VARIABLE rc
    ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Preprocessing failed (${rc}): ${err}")
endif()
