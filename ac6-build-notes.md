# AC6 with the cmsis-executorch Zephyr module

The solution selects AC6. The project carries the following compatibility
adjustments for the installed Zephyr and AC6 6.24 toolchain. The pack's
cmsis-executorch module is unchanged.

## `sys/types.h`

Do **not** add the pack's bare-metal `armclang_shims` directory to Zephyr's
include path. Zephyr already provides `lib/libc/armstdc/include/sys/types.h`,
including `ssize_t`, `mode_t`, `off_t`, `time_t`, and `clock_t`. The pack's
minimal header shadows that header and breaks Zephyr POSIX compilation.

No replacement types shim needs to be ported to cmsis-executorch. A future
module should continue using Zephyr's libc headers when building with AC6.

## libc++ and AEABI portability

The project CMake puts AC6's libc++ headers before the explicitly added C
resource headers, for C++ compilation only. Otherwise libc++ cannot find its
own `stddef.h` wrapper and `nullptr_t` is missing.

`src/zephyr_ac6_compat.hpp` is preincluded in AC6 C++ compilation. It makes
`EDOM`, `ERANGE`, and `EILSEQ` constant expressions with the values defined in
Zephyr's `lib/libc/armstdc/src/errno.c` (33, 34, 138), for `std::errc`.
It also includes the Arm locale header with portability level zero locally,
so libc++ gets the native constant `LC_*` masks. It restores the portability
macro immediately afterward; other APIs retain Zephyr's required level one.

These fixes belong in Zephyr's AC6 toolchain/libc integration (or an AC6
libc++ fix). Until those are available, a future cmsis-executorch module
supporting this combination would need equivalent compatibility handling,
including propagation to application C++ consumers, to avoid per-project fixes.
Copying the bare-metal `sys/types.h` shim would not address these problems.

## armlink and C++ initialization

Unlike the GNU ld path used with GCC, Zephyr's CMake scatter generator leaves
the C++ `ctors` section unconfigured. The project supplies the `.init_array`
input and keeps it during linking. `src/zephyr_ac6_init.c` invokes that table
at application initialization, before `main`, because the CMake linker
generator disables Zephyr's normal `CONFIG_STATIC_INIT_GNU` path. ExecuTorch
depends on these constructors for backend and operator registration.

AC6/armlink emits self-relative constructor entries: each 32-bit word is an
offset from that word's address, including the Thumb bit. The startup hook
adds the slot address before calling it. Treating the word as an absolute
function pointer caused the reported instruction-access fault at
`0xfffdec6c`: the entry at `0x312236c4` held `0xfffdec6d`, which decodes to
`0x31202331`, the Thumb address of `_GLOBAL__sub_I_RegisterAllKernels.cpp`.
GCC uses Zephyr's GNU linker/startup path and did not use this AC6 hook.

The project also compiles Zephyr's existing `lib/cpp/abi/cpp_dtors.c` when
`CONFIG_STATIC_INIT_GNU` is disabled. Otherwise AC6's full runtime supplies
`__cxa_atexit`, which calls `__aeabi_atexit`; Zephyr's ARM wrapper calls back
into `__cxa_atexit`, causing an infinite loop during Ethos-U backend
construction. Zephyr's handler avoids that loop and follows its normal
embedded policy of not registering global destructors for process exit.

Zephyr's armlink setup also hard-codes `--library_type=microlib`, which lacks
functions needed by the full libc++ library. The project changes this to the
standard Arm C runtime for all intermediate and final ELF link targets.
These are generic Zephyr armlink/C++ integration fixes that should be made
upstream. Until then, a module distributing AC6 support needs equivalent
linker and initialization handling, rather than just a types header.

The application enables `CONFIG_COMMON_LIBC_MALLOC` with a 64 KiB arena.
This supplies Zephyr's initialized, synchronized malloc family for C++
allocations. The full Arm runtime's default allocator expects Arm runtime
heap initialization, which Zephyr's reset path does not perform; using it
faulted inside `malloc`. This allocator selection belongs in Zephyr's AC6
libc integration or an interim module configuration. Arena sizing remains
an application choice. The 16 KiB kernel heap used by the Ethos-U driver
is separate.

The application enables `CONFIG_FPU=y` and `CONFIG_FP_HARDABI=y`, as
requested. Zephyr enables the hardware FPU and compiles with the hard-float
calling convention. Previously, with `CONFIG_FPU=n`, armlink selected an
FPU-based `_ffltu` conversion helper despite Zephyr leaving the FPU
disabled; input preparation faulted at its first instruction. FPU/ABI
selection is an application configuration choice and needs no module patch.
If a future module supports FPU-disabled AC6 builds, Zephyr's armlink
integration must select matching software-only libraries (`--fpu=SoftVFP`).

AC6 6.24 reproducibly crashes in the Register Coalescer pass while compiling
`quantize_per_tensor_out` in this pack's `op_quantize.cpp` with hard-float,
at both Zephyr's default `-Og` and CMSIS-style `-O1`. The local CMSIS
reference build uses an older source without the current NaN handling and
double-precision clamping, so its success does not isolate a Zephyr-only
cause. The project applies `-O0` only to `op_quantize.cpp`, and only for
AC6 6.24.0 with hard-float. Other files retain their normal optimization.
This is a compiler workaround, with a performance cost confined to this
operator file. A future module supporting this compiler/source combination
needs an equivalent workaround until a compiler fix is verified. The
reference project is unchanged.

Finally, the armlink link path propagates the app's interface-library sources
to intermediate ELF targets. The generated AI layer exposes `model_pte.c`
this way, causing duplicate model symbols. The local `create_ai_layer.py`
generator's Zephyr backend now emits a deferred ARMClang-only hook that moves
each layer's model sources to `app` so they are compiled once. GCC retains
interface-source propagation. The root CMakeLists needs no layer-specific
workaround. This concerns AI layer generation / Zephyr's armlink link
implementation, rather than the cmsis-executorch runtime module.

## ExecuTorch `size_t`

The solution's existing `-includestddef.h` supplies `size_t` for ExecuTorch's
`tag.h`, which does not include its own dependency. This is independent of
`sys/types.h`. The permanent fix belongs in ExecuTorch's header; until then
the module needs to supply the include for both its sources and consumers.

## Validation

Verified `Corstone-320-FVP` / Debug with AC6 6.24.0, Zephyr 4.4.0, and
CMSIS-Toolbox 2.15.0. The cbuild command completed with exit code zero and
generated `zephyr.elf` and `zephyr.hex`. The linked constructor table contains
two entries (8 bytes), and the application startup hook is present. The
compile database contains one model compilation and no bare-metal shim path.
The final link still warns that the Ethos-U driver archive is listed twice;
armlink ignores the duplicate. See the
[Ethos-U CMake issue report](ethos-u-cmake-duplicate-archive-report.md) for
reproduction steps and dependency analysis. Compilation also reports existing
deprecated API, ctype-macro, and finite-floating-point warnings.

Runtime validation on the Corstone-320 FVP completed the existing inference
test: execution reached the `Test_result: PASS` path after validating one
output tensor with ten float elements, printed PASS, completed cleanup, and
returned zero from `executorch_run_test` to `main`. This validates the
application's existing deterministic-input test, not numerical conformance
against a separate reference output. FPU, hard-float ABI, and FPU context
sharing are enabled; all 185 compile commands use `-mfloat-abi=hard`.
Only `op_quantize.cpp` uses `-O0`; the other 184 retain `-Og`. The physical
DevKit-E8 target was not tested. Temporary debugger settings and breakpoints
were removed after validation.

The shell build used the environment exported in `.cmsis/tools-environment.yml`:

```text
cbuild zephyr.csolution.yml --target all --active Corstone-320-FVP --packs --log out/ac6-shell-build.log
```
