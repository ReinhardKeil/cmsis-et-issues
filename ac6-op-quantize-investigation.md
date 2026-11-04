# AC6 6.24 op_quantize investigation

Investigated on 2026-11-04 with the installed AC6 6.24.0 compiler,
Cortex-M85, and hard-float ABI.

The compiler crash is **not specific to Zephyr**. Two differences obscured
the comparison: the supplied CMSIS command uses `-O3` and the installed
ExecuTorch 1.5.1 source; Zephyr Debug uses `-Og` and a different operator
source from the installed 1.6.0-zephyrpreview1 pack.

| Compile environment | Operator source | `-O0` | `-Og` | `-O1` | `-O2` | `-O3` |
|---|---|---|---|---|---|---|
| Supplied CMSIS flags/headers | 1.5.1 | Pass | Pass | Pass | Pass | Pass |
| Supplied CMSIS flags/headers | Installed preview | Pass | Crash | Crash | Pass | Pass |
| Zephyr flags/headers | 1.5.1 | — | Pass | Pass | — | Pass |
| Zephyr flags/headers | Installed preview | Pass | Crash | Crash | Pass | Pass |

Both environments reproduce the Register Coalescer crash with the installed
preview source at `-Og` and `-O1`. The CMSIS experiments retain the original
1.5.1 include paths and RTE preinclude, changing only the source, optimization
level, and output location. The Zephyr experiments retain Zephyr's headers,
defines, portability mode, and other compiler options. Expected compiler
failures were captured by a temporary CMake target built through
`cmsis_action`; that target was removed afterward.

The relevant scalar source difference is `quantize_val_with_inv_scale`,
which handles NaNs and clamps in double precision before integer conversion.
The older implementation converts to `int64_t` before clamping. Other changes
in this file affect NEON code, which is inactive on Cortex-M85. These results
identify the source/optimization combination that triggers a compiler backend
defect; they do not isolate an individual expression within that helper.
The source labels identify the installed files, not every upstream release
with those version numbers.

The application now overrides only `op_quantize.cpp` to `-O2`, under the
existing AC6 6.24.0/hard-float guard. Other files retain their configured
optimization. `-O3` also passed a complete Zephyr compile and link.

With the final `-O2` override, the complete Zephyr Debug build passed.
The compile database contains 185 commands: only `op_quantize.cpp` appends
`-O2`, overriding its earlier `-Og`; the other 184 retain `-Og`.
The Corstone-320 FVP inference smoke test printed `Test_result: PASS`
after checking one output tensor with ten float elements. This verifies the
existing execution test, not independent numerical conformance or performance.
The temporary breakpoint was removed and the FVP was left running.

Zephyr's project-wide optimization is a Kconfig choice. This solution's
Debug build type explicitly requests `CONFIG_DEBUG_OPTIMIZATIONS=y`
(`-Og`); Release requests `CONFIG_SPEED_OPTIMIZATIONS=y` (`-O2`). The
current target-set images refer to `Zephyr_app.Debug`, so selecting the
Release build also requires a target-set image context using
`Zephyr_app.Release`. A `prj.conf` optimization choice must be consistent
with the solution's command-line Kconfig settings.

Compile evidence is retained in `out/quantize-investigation/`: the
`zephyr-*.log` and `cmsis-*.log` files, `zephyr-results.txt`,
and `cmsis-results.txt`.
Command snapshots are `out/quantize-zephyr-command.json` and
`out/quantize-cmsis-command.json`.
