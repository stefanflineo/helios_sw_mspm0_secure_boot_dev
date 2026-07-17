set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TI_ARM_CLANG_PATH "C:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS" CACHE PATH "TI ARM Clang compiler path")

set(CMAKE_C_COMPILER   "${TI_ARM_CLANG_PATH}/bin/tiarmclang.exe")
set(CMAKE_ASM_COMPILER "${TI_ARM_CLANG_PATH}/bin/tiarmclang.exe")
set(CMAKE_AR           "${TI_ARM_CLANG_PATH}/bin/tiarmar.exe")
set(CMAKE_OBJCOPY      "${TI_ARM_CLANG_PATH}/bin/tiarmobjcopy.exe")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
