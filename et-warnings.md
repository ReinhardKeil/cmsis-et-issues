# CMSIS-ExecuTorch compiler diagnostics

Tested on `Zephyr_app.Debug+Corstone-320-FVP` with PyTorch ExecuTorch `1.6.0-zephyrpreview1` and Zephyr `4.5.0-rc3`; warnings were not disabled.

## Test matrix

| Toolchain | Result |
|---|---|
| Arm Compiler 6.24.0 | Compilation reaches the pack after a manual Zephyr toolchain selection; fails first on `tag.h`, and after the workaround reaches an unrelated duplicate-model link error. |
| Arm GNU 14.3.1 | Clean `cbuild` succeeds with `-includestddef.h`; warnings below are emitted. |
| Arm GNU 15.3.2 | Clean `cbuild` succeeds with `-includestddef.h`; diagnostics match GCC 14.3.1. |
| LLVM Embedded 23.1.0 | Clean build succeeds with `host/llvm`, LLD, compiler-rt, and `-includestddef.h`; direct `cbuild` fails because it selects the obsolete `llvm` variant. |

**GCC version labels:** Arm Tools Environment Manager calls the tested artifact `15.3.2` (Arm GNU Toolchain `15.3.Rel2`, build `arm-15.181`), while GCC itself and CMake report its upstream compiler core as `15.3.1`; these names identify the same tested installation.

## Reported issues

| Issue | Reported problem |
|---|---|
| [Open-CMSIS-Pack/devtools #2685](https://github.com/Open-CMSIS-Pack/devtools/issues/2685) | `west-defs` splits `-include stddef.h` and passes a bare `-include`. |
| [Open-CMSIS-Pack/devtools #2686](https://github.com/Open-CMSIS-Pack/devtools/issues/2686) | cbuild maps CLANG to Zephyr's unsupported `llvm` variant instead of `host/llvm`. |
| [Open-CMSIS-Pack/devtools #2688](https://github.com/Open-CMSIS-Pack/devtools/issues/2688) | `--toolchain AC6@6.24.0` emits an empty `ZEPHYR_TOOLCHAIN_VARIANT`. |

## CMSIS-ExecuTorch source

- DONE: **Error, AC6/GCC:** `include/executorch/runtime/core/tag.h:73,79` uses `size_t` without including `<stddef.h>`; add that include upstream, or temporarily pass the no-space driver option `-includestddef.h` (the current `-include stddef.h` is split by `cbuild`).
- **Warning, AC6/GCC:** `include/executorch/runtime/core/portable_type/optional.h:33` deprecates the `optional` alias while pack headers and sources still use it; replace uses with `std::optional`.
- **Warning, AC6:** `include/executorch/kernels/portable/cpu/util/distance_util.h:119` uses `INFINITY` while the active floating-point mode disallows NaN/Inf; compare with an appropriate finite limit or enable an IEEE mode only if the target contract permits it.
- **Warning, AC6/GCC:** `src/kernels/quantized/cpu/op_quantize.cpp:102,107` promotes `float` operands to `double`; keep the expression consistently `float` or intentionally compute entirely in `double`.
- **Warning, AC6/GCC:** `src/kernels/quantized/cpu/op_dequantize.cpp:478` promotes `float` operands to `double`; use matching operand/result types.
- **Warning, GCC 14/15:** `src/kernels/quantized/cpu/op_quantize.cpp:323` compares `size_t` with signed `input_numel`; validate non-negativity and use one unsigned size type.
- **Warning, GCC 14/15:** `src/kernels/quantized/cpu/op_dequantize.cpp:485,606` compares `size_t` with `ssize_t`; validate the signed value before converting to `size_t`.
- **Note, GCC 14/15:** several functions taking `ArrayRef`/`IntArrayRef` report the GCC 7.1 parameter-passing ABI change; this is informational, but all linked C++ objects must use an ABI-compatible GCC toolchain.
- **Warning, LLVM 23:** `include/executorch/runtime/core/exec_aten/util/dim_order_util.h:26` defines `validate_dim_order` without instantiating it in several translation units; move the template to a narrower header or ensure its intended callers instantiate it.
- **Warning, LLVM 23:** `include/executorch/kernels/portable/cpu/util/copy_ops_util.h:23` defines `_as_strided_copy` without using it in some translation units; move it to the implementation that uses it or instantiate/call it as intended.

## Integration and application diagnostics

- DONE: **Error, cbuild/AC6:** selecting `AC6@6.24.0` with `--toolchain` generates `ZEPHYR_TOOLCHAIN_VARIANT=""`, so Zephyr searches for the Zephyr SDK; use `compiler: AC6` in `zephyr.csolution.yml` and ensure the generated command selects `armclang` ([devtools #2688](https://github.com/Open-CMSIS-Pack/devtools/issues/2688)).
- DONE: **Error, all compilers via cbuild:** `zephyr.csolution.yml:41` passes `CMAKE_CXX_FLAGS: -include stddef.h`, which becomes a bare `-include`; use `-includestddef.h` temporarily or, preferably, fix `tag.h` ([devtools #2685](https://github.com/Open-CMSIS-Pack/devtools/issues/2685)).
- **Warning, CMake/all compilers:** the generated CMSIS-ExecuTorch object path can exceed CMake's safe 250-character Windows limit; use a short output directory.
- DONE: **Warnings, AC6/GCC:** `src/arm_embedded_module.cpp:26` initializes members out of declaration order and `src/arm_embedded_module.hpp:545,546` stores unused PTE fields; reorder the initializer list and remove or use the fields.
- DONE: **Warnings, GCC 14/15:** `src/arm_embedded_module.cpp:70,101,162,225` and `src/executorch_test.cpp:118` compare signed and unsigned counts; validate and use matching index/count types.
- DONE: **Error, AC6 linker:** `ai_layer/model_pte.c` is linked through both the application and Zephyr pre-link, multiplying `model_pte` and `model_pte_size`; include the generated model object only once.
- **Warning, AC6 linker:** `libethosu_core_driver.a` is supplied twice; remove one link dependency.
- DONE: **Warnings, GCC 14/15 linker:** the generated `DT_N_NODELABEL_sram0_P_zephyr_memory_region_STRING_TOKEN` section is orphaned and the ELF has an RWX load segment; give the temporary pool a valid iterable/NOLOAD linker placement with explicit permissions.

## cbuild CLANG-to-Zephyr integration failure

Tracked as [Open-CMSIS-Pack/devtools #2686](https://github.com/Open-CMSIS-Pack/devtools/issues/2686).

This is a CMSIS-Toolbox integration issue. With `compiler: CLANG`, cbuild 2.15 detects LLVM Embedded 23.1.0 but generates:

```text
cmake -E env LLVM_TOOLCHAIN_PATH=".../23.1.0/" ZEPHYR_TOOLCHAIN_VARIANT="llvm" west build ...
```

Zephyr 4.5 supports Arm Toolchain for Embedded through `ZEPHYR_TOOLCHAIN_VARIANT=host/llvm`; its tree contains `cmake/toolchain/host/llvm/generic.cmake`, not `cmake/toolchain/llvm/generic.cmake`. The generated `llvm` value therefore fails in `FindHostTools.cmake` before compiler detection, followed by the secondary `dt_preprocess() missing required argument: CPP` error because no C preprocessor was configured.

The authoritative setup is Zephyr's [Arm Toolchain for Embedded documentation](https://docs.zephyrproject.org/latest/develop/toolchains/arm_toolchain_for_embedded.html), whose checked-out source is `C:/w/zephyrproject/zephyr/doc/develop/toolchains/arm_toolchain_for_embedded.rst:23-24,66,85`; it explicitly specifies `host/llvm`, `LLVM_TOOLCHAIN_PATH`, and the LLD/compiler-rt options used by the successful test.

The cbuild integration should emit `host/llvm` and select the LLVM runtime/linker settings required for this embedded target. Until that is fixed, the equivalent clean Zephyr build works with:

```text
ZEPHYR_TOOLCHAIN_VARIANT=host/llvm
LLVM_TOOLCHAIN_PATH=<Arm LLVM Embedded 23.1.0 root>
CONFIG_LLVM_USE_LLD=y
CONFIG_COMPILER_RT_RTLIB=y
```

Do not create a compatibility copy at `cmake/toolchain/llvm`: it would modify Zephyr and conceal the stale cbuild mapping.

## DONE: Fixing the duplicate embedded model with AC6

`ai_layer/CMakeLists.txt:15-23` puts `model_pte.c` in the `INTERFACE_SOURCES` of `ai_layer` and later links that interface library to `app`. Zephyr's pre-link construction consumes the interface source a second time: `build.ninja` therefore links both `CMakeFiles/app.dir/ai_layer/model_pte.o` and `zephyr/CMakeFiles/zephyr_pre0.dir/.../ai_layer/model_pte.o`. Both objects define the strong external symbols `model_pte` and `model_pte_size`, so Arm `armlink` correctly reports `L6200E` for each symbol.

Fix the AI-layer generator, or make the equivalent generated-file change, so the model source belongs directly and exclusively to Zephyr's `app` target. Replace the `add_library(ai_layer INTERFACE)` block and its deferred `target_link_libraries()` call in `ai_layer/CMakeLists.txt` with:

```cmake
# Zephyr creates app during find_package(), after this directory is processed.
get_directory_property(_ai_layer_parent PARENT_DIRECTORY)
cmake_language(DEFER DIRECTORY "${_ai_layer_parent}"
  CALL target_sources app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/model_pte.c"
)
cmake_language(DEFER DIRECTORY "${_ai_layer_parent}"
  CALL target_include_directories app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
```

Do not add `model_pte.c` anywhere else and do not make either symbol weak; weak definitions would hide the build-graph defect and could select the wrong model. After regenerating the AI layer, perform a clean build and verify that `build.ninja` contains one compile edge for `ai_layer/model_pte.c` and that the pre-link command contains only `CMakeFiles/app.dir/ai_layer/model_pte.*`.

## Duplicate Ethos-U core-driver archive with AC6

Arm `armlink` reports:

```text
Warning: L6304W: Duplicate input file modules/hal_ethos_u/ethos-u-core-driver/libethosu_core_driver.a ignored.
```

The generated `zephyr_pre0.rsp` and final-link response contain the same archive twice, consecutively:

```text
modules/hal_ethos_u/ethos-u-core-driver/libethosu_core_driver.a
modules/hal_ethos_u/ethos-u-core-driver/libethosu_core_driver.a
```

The application does not build two different copies of the driver. Both arguments resolve to the same CMake target and archive, so `armlink` uses the first and ignores the second. The warning is therefore harmless and is unrelated to unresolved symbols, the embedded model, or the invalid U55 FVP flash-region size.

Zephyr registers the driver in `zephyr/modules/hal_ethos_u/CMakeLists.txt` with:

```cmake
zephyr_link_libraries(ethosu_core_driver)
```

Only one explicit driver registration was found. The duplicate is introduced while Zephyr constructs the ArmClang pre-link and final-link command, rather than by `CMSIS-et-template` or the CMSIS-ExecuTorch pack. The proper fix belongs in Zephyr's ArmClang link-command generation: identical archive paths should be emitted once. If Zephyr deliberately repeats static libraries for linkers that require rescanning, its ArmClang integration should instead avoid the repetition because `armlink` already handles the archive and diagnoses the duplicate.

Do not remove the Ethos-U dependency from the application template merely to silence `L6304W`; that would hide the integration defect and could break driver availability or link ordering with another toolchain.

## AC6 compiler crash in `op_quantize.cpp`

An AC6 6.24 hard-float Zephyr Debug build can fail while compiling the
ExecuTorch quantize operator:

```text
error: clang frontend command failed due to signal (use -v to see invocation)
ninja FAILED: .../kernels/quantized/cpu/op_quantize.o
```

This is an internal compiler failure, not a C++ diagnostic and not a linker
error. The failure occurs in AC6's optimization pipeline when this translation
unit is compiled at Zephyr's Debug optimization level. Compiling only
`op_quantize.cpp` with `-O2` avoids the failing optimizer path while leaving the
rest of the application at its selected Debug or Release optimization level.

The template applies the workaround after the CMSIS-ExecuTorch Zephyr module
has created its CMake target:

```cmake
if(CMAKE_CXX_COMPILER_ID STREQUAL "ARMClang" AND CONFIG_FP_HARDABI)
  get_target_property(_et_sources cmsis_executorch SOURCES)
  set(_et_quantize_workaround_applied FALSE)
  foreach(_source IN LISTS _et_sources)
    if(_source MATCHES "[/\\\\]op_quantize\\.cpp$")
      set_property(SOURCE "${_source}" TARGET_DIRECTORY cmsis_executorch
        APPEND PROPERTY COMPILE_OPTIONS -O2)
      set(_et_quantize_workaround_applied TRUE)
    endif()
  endforeach()
  if(NOT _et_quantize_workaround_applied)
    message(FATAL_ERROR
      "AC6 workaround could not find ExecuTorch op_quantize.cpp")
  endif()
endif()
```

Do not gate the workaround on an exact textual compiler version. CMake and the
toolchain can expose equivalent AC6 versions with different version strings,
which previously allowed the source-specific option to be skipped. The
configure-time assertion also prevents a future pack layout or target-name
change from silently disabling the workaround. A clean U55 Corstone-300 FVP
rebuild with AC6 6.24 succeeded after applying this form.

### Why the CMSIS build did not fail

The CMSIS build does include the quantize operator, but it does not compile the
same revision of `op_quantize.cpp`:

- `cmsis-u55` and `cmsis-u85` use `PyTorch::ExecuTorch@1.5.1`.
- `zephyr-u55` and `zephyr-u85` use
  `PyTorch::ExecuTorch@1.6.0-zephyrpreview1`.

In 1.6.0-zephyrpreview1 the file gained the templated
`quantize_val_with_inv_scale()` helper. It performs mixed `float`/`double`
clamping and is instantiated for several input types, including half precision,
and several integer output types. The same helper was also added to scalar
fallbacks in the Arm-optimized paths. Compared with the 1.5.1 implementation,
this creates a larger and more complex set of template instantiations for the
optimizer and exposes the AC6 6.24 failure at the Zephyr Debug optimization
setting.

Therefore the relevant distinction is not CMSIS versus Zephyr source
selection: both select the quantized operator. It is primarily the different
ExecuTorch pack/source revision combined with the Zephyr hard-float Debug
compile settings. The `-O2` setting is a narrow workaround for that translation
unit; the underlying compiler crash should still be reported to the AC6 team
with a preprocessed 1.6.0-zephyrpreview1 reproducer.

