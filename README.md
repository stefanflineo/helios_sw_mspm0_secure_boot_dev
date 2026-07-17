# Customer Secure Code — MSPM0L1117 (VS Code / CMake port)

This is a straight port of TI's `customer_secure_code_LP_MSPM0L1117_nortos_ticlang`
CCS example to the CMake + Ninja + `tiarmclang` VS Code setup used by the
Helios project, so it can be opened, built, and debugged the same way.

No functional code was changed — this only replaces the CCS/`makefile`
build system with CMake and reorganizes files on disk. See "What moved
where" below.

## Prerequisites

Same as Helios:

- MSPM0 SDK 2.10 (`MSPM0_SDK` cache var, default `C:/ti/mspm0_sdk_2_10_00_04`)
- TI ARM Clang toolchain, e.g. from CCS's `ti-cgt-armllvm_5.1.1.LTS`
  (`TI_ARM_CLANG_PATH` cache var)
- CMake >= 3.22, Ninja
- VS Code with the CMake Tools extension (and cortex-debug if you want to
  flash/debug from `launch.json`)

If your SDK/toolchain live somewhere else, edit the defaults in
`CMakePresets.json` (or override with `-DMSPM0_SDK=... -DTI_ARM_CLANG_PATH=...`).

## Building

From VS Code: `Ctrl+Shift+B` (runs the "CMake build" task), or from a
terminal:

```
cmake --preset debug-tiarmclang-ninja
cmake --build --preset debug
```

Output: `build/debug/customer_secure_code.out` and the flashable
`build/debug/customer_secure_code-bank1-0x10000.bin`.

## What moved where

| Original (CCS project root)                        | New location                                   |
|------------------------------------------------------|-------------------------------------------------|
| `*.c` / `*.h` (aes_cmac, keystore, secret, etc.)      | `src/app/`                                       |
| `flash_map_backend/`, `mcuboot_config/`, `sysflash/`, `third_party/`, `ti/driverlib/` | `src/app/<same subfolder>` (unchanged, so relative `#include`s inside mcuboot/TI sources keep working) |
| `customer_secure_code.syscfg`                         | `generated/`                                     |
| `Debug/syscfg/ti_msp_dl_config.{c,h}`                 | `generated/`                                     |
| `Debug/syscfg/boot_config.{c,h}`                      | `generated/`                                     |
| `Debug/syscfg/customer_secure_config.h`               | `generated/`                                     |
| `mspm0l1117.cmd`                                      | `linker/`                                        |
| CCS `.project` / `.cproject` / `Debug/makefile`       | `CMakeLists.txt`, `CMakePresets.json`, `tiarmclang-toolchain.cmake` |

The SysConfig-generated files under `generated/` were pulled from the
project's last `Debug` build output and checked in as-is (same approach
Helios uses for `generated/ti_msp_dl_config.c`). If you change
`customer_secure_code.syscfg`, re-run SysConfig and overwrite these files —
there's no SysConfig CMake step wired up yet.

## Notes / things deliberately left out for now

- This does **not** replicate Helios's modular `hal_drv` / `device_drv`
  CMake package system — this project is a single flat application, same
  as the original CCS layout, just recompiled with CMake. That can be
  layered on later if this code grows shared drivers.
- `targetConfigs/` (CCS-specific debug probe config) was not carried over;
  `.vscode/launch.json` instead points `cortex-debug` at generic
  `interface/xds110.cfg` / `target/ti_mspm0.cfg` OpenOCD config files, same
  as Helios. Adjust to match your probe.
- Build flags (`-Oz -flto -mcpu=cortex-m0plus -march=thumbv6m -mfloat-abi=soft
  -mlittle-endian -mthumb -gdwarf-3`, `-D__MSPM0L1117__ -DEXCLUDE_TRACE`) and
  the link step (`bimsupport.a` + `driverlib.a` + `libc.a`, `--rom_model`,
  `--heap_size=128`, `--stack_size=256`) were copied verbatim from the
  original CCS `Debug/makefile` and `Debug/subdir_rules.mk` to keep behavior
  identical.
