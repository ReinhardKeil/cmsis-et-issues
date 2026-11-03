# Zephyr Ethos-U integration lists the core-driver archive twice with AC6

## Problem

Building a Zephyr application with Arm Compiler 6 succeeds, but both the
intermediate and final link report:

```text
Warning: L6304W: Duplicate input file modules/hal_ethos_u/ethos-u-core-driver/libethosu_core_driver.a ignored.
```

The generated link inputs contain the same archive twice. The archive has one
build rule, and each of its three source files has one compilation command.
This is a duplicate link input, not duplicate driver compilation.

## Environment

| Item | Observed configuration |
|---|---|
| Zephyr | 4.4.0 |
| Zephyr board | `mps4/corstone320/fvp` |
| NPU | Ethos-U85, 256 MACs |
| Compiler/linker | Arm Compiler 6.24.0, `armclang` / `armlink` |
| CMake / Ninja | 3.31.12 / 1.13.2 |
| Build orchestration | CMSIS-Toolbox 2.15.0, Debug |
| Host | Windows |
| Application | CMSIS-ExecuTorch inference application |
| ExecuTorch pack | `PyTorch::ExecuTorch@1.6.0-zephyrpreview1` |
| `hal_ethos_u` revision declared by Zephyr's manifest | `03567073fe2b9802c0bd73f9534da6f8a03924d1` |

The revision above is the manifest pin, not an independently verified module
HEAD. Relevant generated configuration:

```ini
CONFIG_MULTITHREADING=y
CONFIG_ETHOS_U=y
CONFIG_ETHOS_U_NPU_NAME="ethos-u85-256"
CONFIG_FPU=y
CONFIG_FP_HARDABI=y
```

FPU and hard-float describe the tested build; they have not been established
as necessary to reproduce this warning.

## Reproduction in the reporting application

1. Select `Corstone-320-FVP`, Debug, with `compiler: AC6` in
   `zephyr.csolution.yml`.
2. Build with the CMSIS Solution **Build** action. The corresponding command,
   in the configured CMSIS/Zephyr tools environment from the application root,
   is:

   ```text
   cbuild zephyr.csolution.yml --target all --active Corstone-320-FVP --packs --log out/ethos-u-duplicate-archive.log
   ```

3. On a build that performs linking, observe `L6304W` for `zephyr_pre0.elf`
   and `zephyr.elf`. An up-to-date build that only reports "no work to do"
   will not re-emit linker warnings; its existing link inputs can still be
   inspected as described below.
4. Inspect the `LINK_LIBRARIES` entries for those two targets in
   `out/Zephyr_app/Corstone-320-FVP/Debug/build.ninja`.

This is a reproduction in the existing application, which already contains
other AC6 compatibility adjustments. An independent minimal Zephyr application
has not been tested for this issue.

## Evidence

The existing AC6 build graph was inspected on 2026-11-03:

| Item | Count |
|---|---:|
| Archive occurrences in `zephyr_pre0.elf` link inputs | 2 |
| Archive occurrences in `zephyr.elf` link inputs | 2 |
| Build rules creating `libethosu_core_driver.a` | 1 |
| Compilation commands for `ethosu_driver.c` | 1 |
| Compilation commands for `ethosu_pmu.c` | 1 |
| Compilation commands for `ethosu_device_u85.c` | 1 |

The following abbreviated excerpt represents both link input lists:

```text
LINK_LIBRARIES = ... modules/hal_ethos_u/ethos-u-core-driver/libethosu_core_driver.a ... modules/hal_ethos_u/ethos-u-core-driver/libethosu_core_driver.a ...
```

The counts come from `build.ninja` and `compile_commands.json`, not a linker
map. No driver or Zephyr source changes were made during this inspection.

## CMake dependency analysis

The integration code is in the **Zephyr repository**, at
`modules/hal_ethos_u/CMakeLists.txt:29`, rather than the core driver's own
top-level `CMakeLists.txt`:

```cmake
target_link_libraries(ethosu_core_driver PUBLIC
   zephyr_interface)

zephyr_link_libraries(ethosu_core_driver)
```

In `cmake/modules/extensions.cmake:131`, the non-`PROPERTY` branch of
`zephyr_link_libraries()` expands to:

```cmake
target_link_libraries(zephyr_interface INTERFACE ${ARGV})
```

These declarations therefore create a circular link dependency:

```text
zephyr_interface
  -> ethosu_core_driver
       -> zephyr_interface
```

Zephyr's AC6 link construction in `cmake/linker/armlink/target.cmake:64`
also collects the `LINK_LIBRARIES` of Zephyr libraries into the ELF targets:

```cmake
foreach(lib ${ZEPHYR_LIBS_PROPERTY})
  if(NOT ${lib} STREQUAL arch__arm__core__cortex_m)
    list(APPEND ZEPHYR_LIBS_OBJECTS $<TARGET_OBJECTS:${lib}>)
    list(APPEND ZEPHYR_LIBS_OBJECTS $<TARGET_PROPERTY:${lib},LINK_LIBRARIES>)
  endif()
endforeach()
```

The circular dependency is confirmed by the source declarations, and the
duplicate archive is confirmed in the generated link inputs. The cycle is the
suspected cause of CMake repeating the static archive; no patch breaking the
cycle has yet been tested to isolate that causal link.

The core driver's `zephyr/module.yml` declares `cmake-ext: True`, which directs
integration through Zephyr's external module CMake file. This makes Zephyr's
Ethos-U integration and armlink integration the relevant review locations.

## Impact and requested correction

`armlink` explicitly ignores the duplicate and the build completes. There is
no observed runtime failure attributable to this warning. The tested GCC
15.3.2 build did not report this duplicate-input diagnostic; absence of that
warning does not establish that GCC has a different dependency graph.

Please review how the driver inherits Zephyr's compilation requirements
without introducing a circular link dependency through `zephyr_interface`.
The resulting AC6 link inputs should contain the archive once while retaining
all required driver symbols and usage requirements.

A possible direction is to separate the driver's compile-time use of
`zephyr_interface` from the firmware's dependency on `ethosu_core_driver`.
Any implementation must preserve Zephyr include directories, compile options
and definitions, as well as the driver's public usage requirements. Merely
changing `PUBLIC` to `PRIVATE` still leaves a link dependency and is not a
demonstrated fix. No specific replacement patch is proposed as validated here.

Suggested acceptance checks:

- AC6 links both intermediate and final ELFs without `L6304W` for this archive.
- Each relevant link invocation contains the driver archive once.
- Driver source files remain compiled once with the required Zephyr settings.
- GCC still builds, and Ethos-U initialization/inference still works.

This warning is separate from the AI layer's previously fixed duplicate
`model_pte` definitions. The evidence does not indicate that the AI-layer
generator or the `cmsis-executorch` runtime needs changing for this issue.
