cmake_minimum_required(VERSION 3.22)
project(customer_secure_code C ASM)

set(MSPM0_SDK "C:/ti/mspm0_sdk_2_10_00_04" CACHE PATH "MSPM0 SDK path")
set(TI_ARM_CLANG_PATH "C:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS" CACHE PATH "TI ARM Clang path")

# ---------------------------------------------------------------------------
# Application sources
#
# Per the original CCS project, everything below lives in a flat directory
# (project root) with a handful of subfolders (flash_map_backend,
# mcuboot_config, sysflash, third_party, ti/driverlib). That has been
# preserved as-is under src/app so relative #includes inside the mcuboot
# sources (e.g. "flash_map_backend/flash_map_backend.h") keep working
# unmodified.
# ---------------------------------------------------------------------------
set(APP_SOURCES
    src/app/aes_cmac.c
    src/app/boot_keys.c
    src/app/customer_secure_code.c
    src/app/hash_verify.c
    src/app/keystore.c
    src/app/lockable_storage_private.c
    src/app/rollback.c
    src/app/secret.c
    src/app/startup_mspm0l111x_ticlang.c

    src/app/flash_map_backend/flash_map_backend.c
    src/app/mcuboot_config/trace.c

    src/app/third_party/mcuboot/boot/bootutil/src/boot_record.c
    src/app/third_party/mcuboot/boot/bootutil/src/bootutil_misc.c
    src/app/third_party/mcuboot/boot/bootutil/src/bootutil_public.c
    src/app/third_party/mcuboot/boot/bootutil/src/caps.c
    src/app/third_party/mcuboot/boot/bootutil/src/encrypted.c
    src/app/third_party/mcuboot/boot/bootutil/src/fault_injection_hardening.c
    src/app/third_party/mcuboot/boot/bootutil/src/fault_injection_hardening_delay_rng_mbedtls.c
    src/app/third_party/mcuboot/boot/bootutil/src/image_ec.c
    src/app/third_party/mcuboot/boot/bootutil/src/image_ec256.c
    src/app/third_party/mcuboot/boot/bootutil/src/image_ed25519.c
    src/app/third_party/mcuboot/boot/bootutil/src/image_rsa.c
    src/app/third_party/mcuboot/boot/bootutil/src/image_validate.c
    src/app/third_party/mcuboot/boot/bootutil/src/loader.c
    src/app/third_party/mcuboot/boot/bootutil/src/swap_misc.c
    src/app/third_party/mcuboot/boot/bootutil/src/swap_move.c
    src/app/third_party/mcuboot/boot/bootutil/src/swap_scratch.c
    src/app/third_party/mcuboot/boot/bootutil/src/tlv.c

    src/app/third_party/mcuboot/ext/mbedtls-asn1/src/asn1parse.c
    src/app/third_party/mcuboot/ext/mbedtls-asn1/src/platform_util.c
    src/app/third_party/mcuboot/ext/msp-crypto/msp_crypto.c

    # DriverLib sources not present in the prebuilt driverlib.a — shipped
    # locally by the original CCS project, kept here as-is.
    src/app/ti/driverlib/dl_aesadv.c
    src/app/ti/driverlib/dl_flashctl.c

    # SysConfig-generated sources, checked in (see generated/README notes).
    generated/ti_msp_dl_config.c
    generated/boot_config.c
)

# ---------------------------------------------------------------------------
# Common INTERFACE target — guarantees uniform flags across every .o and
# centralizes include paths, mirroring the Helios project's strategy.
# ---------------------------------------------------------------------------
add_library(customer_secure_common_flags INTERFACE)

target_compile_definitions(customer_secure_common_flags INTERFACE
    __MSPM0L1117__
    EXCLUDE_TRACE
)

target_compile_options(customer_secure_common_flags INTERFACE
    -mcpu=cortex-m0plus
    -march=thumbv6m
    -mfloat-abi=soft
    -mlittle-endian
    -mthumb
    -Oz
    -flto
    -gdwarf-3
)

# Include paths. Local/project paths are listed first so that the project's
# own copies of shared mcuboot headers win over anything the SDK may also
# ship, matching what the original CCS project resolved to.
target_include_directories(customer_secure_common_flags INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/third_party/mcuboot/ext

    ${MSPM0_SDK}/source
    ${MSPM0_SDK}/source/third_party/mcuboot/boot/bootutil/include
    ${MSPM0_SDK}/source/third_party/mcuboot/ext/mbedtls-asn1/include
    ${MSPM0_SDK}/source/third_party/mcuboot/ext
    ${MSPM0_SDK}/source/ti/boot_manager/ecc/include
    ${MSPM0_SDK}/source/ti/boot_manager/sha2sw/inc
    ${MSPM0_SDK}/source/third_party/CMSIS/Core/Include
)

# ---------------------------------------------------------------------------
# Final executable
# ---------------------------------------------------------------------------
add_executable(customer_secure_code.out
    ${APP_SOURCES}
)

target_link_options(customer_secure_code.out PRIVATE
    -mcpu=cortex-m0plus
    -march=thumbv6m
    -mfloat-abi=soft
    -mlittle-endian
    -mthumb
    -Oz
    -flto
    -gdwarf-3
    -D__MSPM0L1117__
    -DEXCLUDE_TRACE
    -Xlinker --rom_model
    -Wl,--heap_size=128
    -Wl,--stack_size=256
    ${CMAKE_CURRENT_SOURCE_DIR}/linker/mspm0l1117.cmd
    "-Wl,-m,customer_secure_code.map"
    -Wl,--diag_wrap=off
    -Wl,--display_error_number
    -Wl,--warn_sections
)

target_link_libraries(customer_secure_code.out PRIVATE
    customer_secure_common_flags
    "-Wl,-i${TI_ARM_CLANG_PATH}/lib"
    "-Wl,-l${MSPM0_SDK}/source/ti/boot_manager/lib/ticlang/m0p/bimsupport.a"
    "-Wl,-l${MSPM0_SDK}/source/ti/driverlib/lib/ticlang/m0p/mspm0l111x/driverlib.a"
    -Wl,-llibc.a
)

set_target_properties(customer_secure_code.out PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_NAME customer_secure_code
    SUFFIX .out
)

# ---------------------------------------------------------------------------
# Post-build: generate both flashable images from customer_secure_code.out —
#   customer_secure_code-bank1-0x10000.bin        main flash, 0x0-0x44A8
#   customer_secure_code-nonmain-0x41c00000.bin    NONMAIN (BCR+BSL config),
#                                                   0x41C00000-0x41C00160
#
# Delegated to cmake/generate_bins.cmake (invoked once, via execute_process)
# rather than two chained, manually-quoted COMMAND clauses: chaining a second
# manually-quoted custom command onto POST_BUILD via && is what corrupts
# CMake's Windows/Ninja escaping (produces a malformed build.ninja that fails
# to lex). Routing both tiarmobjcopy calls through one small script sidesteps
# that entirely — no shell string is built to mis-escape.
# ---------------------------------------------------------------------------
add_custom_command(TARGET customer_secure_code.out POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DOBJCOPY=${TI_ARM_CLANG_PATH}/bin/tiarmobjcopy.exe
            -DOUT_FILE=$<TARGET_FILE:customer_secure_code.out>
            -DBIN_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_bins.cmake
    COMMENT "Generating customer_secure_code-bank1-0x10000.bin (main flash) and customer_secure_code-nonmain-0x41c00000.bin (NONMAIN @ 0x41C00000)"
    VERBATIM
)