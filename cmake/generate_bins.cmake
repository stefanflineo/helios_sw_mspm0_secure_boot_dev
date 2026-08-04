# ---------------------------------------------------------------------------
# Post-build helper: generates both flashable .bin images from
# customer_secure_code.out.
#
# Invoked once via a single add_custom_command(... POST_BUILD) in
# CMakeLists.txt, e.g.:
#   cmake -DOBJCOPY=<path> -DOUT_FILE=<path> -DBIN_DIR=<dir> -P generate_bins.cmake
#
# Kept as a separate script (rather than two manually-quoted COMMAND clauses
# chained with &&) because CMake's Ninja generator on Windows mis-escapes
# multiple manually-quoted custom commands chained on the same POST_BUILD
# line, producing a corrupted build.ninja. execute_process() here invokes
# each tiarmobjcopy call directly, with normal CMake list-argument handling,
# so there's no shell string to mis-escape.
# ---------------------------------------------------------------------------

foreach(_var OBJCOPY OUT_FILE BIN_DIR)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "generate_bins.cmake: required variable ${_var} not set")
    endif()
endforeach()

# --- Main flash image: 0x00000000-0x000044A8 -------------------------------
# Everything except the NONMAIN config sections.
execute_process(
    COMMAND "${OBJCOPY}" "${OUT_FILE}"
            -R .BCRConfig -R .BSLConfig -R .TI.bound*
            --output-target binary
            "${BIN_DIR}/customer_secure_code-bank1-0x10000.bin"
    RESULT_VARIABLE _mainflash_result
)
if(NOT _mainflash_result EQUAL 0)
    message(FATAL_ERROR "Failed to generate customer_secure_code-bank1-0x10000.bin (exit ${_mainflash_result})")
endif()

# --- NONMAIN image: 0x41C00000-0x41C00160 -----------------------------------
# .BCRConfig  -> BCR_CONFIG region, origin 0x41C00000, length 0x100
# .BSLConfig  -> BSL_CONFIG region, origin 0x41C00100, length 0x80
# NONMAIN is a physically separate flash array from main flash, so it can't
# be appended into the bin above — it's flashed as its own independent write.
execute_process(
    COMMAND "${OBJCOPY}" "${OUT_FILE}"
            --only-section=.BCRConfig --only-section=.BSLConfig
            --output-target binary
            "${BIN_DIR}/customer_secure_code-nonmain-0x41c00000.bin"
    RESULT_VARIABLE _nonmain_result
)
if(NOT _nonmain_result EQUAL 0)
    message(FATAL_ERROR "Failed to generate customer_secure_code-nonmain-0x41c00000.bin (exit ${_nonmain_result})")
endif()