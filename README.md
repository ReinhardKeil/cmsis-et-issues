# CMSIS-ExecuTorch with Zephyr on Alif E8

The CMSIS solution also provides a `Corstone-320-FVP` target for running the
same Zephyr application on the Arm Corstone-320 Fixed Virtual Platform. Select
that target in the CMSIS Solution view, build it, then use **Run**. From a Zephyr
command line, the equivalent flow is:

```sh
west build -b mps4/corstone320/fvp applications/my_app
west build -t run
```

CMSIS Run and Debug share [`fvp.config`](fvp.config), which selects the secure
ITCM boot vectors, 16 secure MPU regions, a 256-MAC Ethos-U85, and unbuffered
UART0 output on stdout. It also disables graphical and telnet terminal windows.
The equivalent direct model invocation from this application folder is:

```sh
FVP_Corstone_SSE-320 -f fvp.config -a out/Zephyr_app/Corstone-320-FVP/Debug/zephyr/zephyr.elf
```

[`fvp.overlay`](fvp.overlay) maps the console to the installed SSE-320 FVP's
UART0 window at `0x59303000` and disables unused UART3/UART4, whose pin setup
faults on this model. The inference scratch pool uses default system SRAM on
FVP; the Alif target retains its `sram0` placement.

The FVP executable is supplied by the `arm:models/arm/avh-fvp` artifact. If it
is installed elsewhere, set `ARMFVP_BIN_PATH` to the directory containing
`FVP_Corstone_SSE-320` before running the Zephyr target.

This application is cloned into `applications/my_app` in a `west` workspace. The
commands below use `Bash` and place that workspace at
`$HOME/zephyr-repo`. Install `Git` and either `uv` or `Python 3.12`
first. The `CMSIS Solution` extension and an Arm compiler are needed to build
the firmware.

## 1. Recreate the Zephyr workspace

Run these steps in order from a `Bash` shell. Adjust the root path throughout if
you want the workspace somewhere else.

### Step 1: Create the workspace and clone Zephyr without its full history

~~~bash
mkdir -p "$HOME/zephyr-repo"
cd "$HOME/zephyr-repo"
git clone --depth 1 --single-branch --branch v4.4.0 https://github.com/zephyrproject-rtos/zephyr.git zephyr
~~~

The `v4.4.0` tag matches the manifest below. If you want Zephyr's **full `Git`
history and branches**, replace the last command with these two commands:

~~~bash
git clone https://github.com/zephyrproject-rtos/zephyr.git zephyr
git -C zephyr checkout v4.4.0
~~~

The full clone takes substantially more disk space and time. To get full
history for the other `west` projects too, remove their `clone-depth` entries
from the manifest in step 3 and run `west update` without `-n` or
`-o=--depth=1` in step 5.

### Step 2: Create `applications/my_app` from this repository

~~~bash
mkdir -p applications
git clone --depth 1 https://github.com/christophe0606/zephyr-cmsis-executorch-test-app.git applications/my_app
~~~

The application repository includes its CMSIS solution, model, sources, and
`VS Code` settings. The parent `applications` directory will hold the `west`
manifest created next. Omit `--depth 1` here as well if you want the full
application history.

### Step 3: Create `applications/west.yml`

Open the manifest file from `zephyr-repo`:

~~~bash
nano applications/west.yml
~~~

Paste this complete manifest and save it. It imports Zephyr's module list,
excluding upstream ExecuTorch. CMSIS-ExecuTorch comes from an external pack
configured in the application's csolution, rather than from `west`.

~~~yaml
manifest:
  version: "0.10"
  self:
    path: applications
  projects:
    - name: zephyr
      url: https://github.com/zephyrproject-rtos/zephyr.git
      revision: v4.4.0
      path: zephyr
      clone-depth: 1
      import:
        name-blocklist:
          - executorch
~~~

The Zephyr release matches this application. For the full-history variant,
delete its `clone-depth: 1` line before saving the file.

### Step 4: Create one root Python environment and install `west`

Choose **one** option and stay in `zephyr-repo`. Building the existing AI layer
does not require installing model export packages.

With `uv`:

~~~bash
uv venv --python 3.12 --seed .venv
uv pip install --python .venv/bin/python west
~~~

With `Python` and `pip` only:

~~~bash
python3.12 -m venv .venv
.venv/bin/python -m pip install west
~~~

The rest of the guide calls executables inside `.venv` explicitly, so shell
activation is optional.

If you used `uv`, `uv run west` is a shorter way to run the same `west`
commands. It finds the root `.venv` from `zephyr-repo` and from
`applications/my_app`. For example, after `west init` in step 5, run this
from the workspace root:

~~~bash
uv run west update -n -o=--depth=1
~~~

The full `.venv/bin/west` paths below work with either environment setup method.

### Step 5: Set `applications/west.yml` as the manifest and update

~~~bash
.venv/bin/west init -l applications
.venv/bin/west config manifest.project-filter -- '-.*,+zephyr,+cmsis,+cmsis_6,+hal_ethos_u,+hal_alif'
.venv/bin/west update -n -o=--depth=1
~~~

`west init -l applications` creates `.west/config` with `applications/west.yml` as
the workspace manifest. The project filter disables all imported projects,
then enables the listed names. `hal_alif` is retained for compatibility,
although Zephyr `v4.4.0` does not declare a project of that name. The Zephyr
repository itself is already present from step 1.

For `west update`, `-n` (`--narrow`) fetches just the requested revisions and
omits tags. `-o=--depth=1` asks `Git` for shallow fetches of imported projects
that lack `clone-depth`. The project filter
limits **which repositories** are downloaded; these options limit `Git`
history. See the [`west update` options](https://docs.zephyrproject.org/latest/develop/west/built-in.html#west-update)
and [project-filter configuration](https://docs.zephyrproject.org/latest/develop/west/config.html#manifest-project-filter).

For the full-history variant, use this update command instead:

~~~bash
.venv/bin/west update
~~~

Keep the same project filter if you still want only the listed repositories.

### Step 6: Install Zephyr's build Python packages

~~~bash
.venv/bin/python -m pip install -r zephyr/scripts/requirements-base.txt
~~~

`requirements-base.txt` contains the Python packages needed for Zephyr builds
without the broader test and compliance packages. The CMSIS Solution settings
below supply `ZEPHYR_BASE`, so `CMake` can find this checkout without registering
it separately. Zephyr, `west`, the selected modules, and the root Python
environment are now configured. If another application needs a
different module, add its project name to the filter and run `west update`
again.

## 2. Use the external CMSIS-ExecuTorch pack

This application uses the self-contained Zephyr module in:

~~~text
C:/Users/chrfav01/benchresults/executorch_matthias/pack-output/PyTorch.ExecuTorch.1.6.0
~~~

The pack supplies `zephyr/module.yml` with the module name `cmsis-executorch`,
plus the runtime, operators, headers, and FlatBuffers headers. Neither
`executorch` nor `cmsis-executorch` is a west project. Existing checkouts in
`modules/lib` are not used by this application.

Set the pack root through `EXTRA_ZEPHYR_MODULES` in
[`zephyr.csolution.yml`](zephyr.csolution.yml), as shown below. Adjust that
path if the pack is moved. No separate ExecuTorch checkout or Python export
setup is needed to build the existing model.

## 3. Configure the CMSIS solution for `west`

The cloned application already contains [`zephyr.csolution.yml`](zephyr.csolution.yml).
For a new application, create a CMSIS solution with a `west` project, an
application path, the Zephyr board target, and any required `west`/`CMake`
definitions. These are the west-specific parts of this application's
solution:

~~~yaml
solution:
  target-types:
    - type: DevKit-E8
      board: DevKit-E8
      device: AE822FA0E5597LS0:M55_HP
      variables:
        - west-board: ensemble_e8_dk/ae822fa0e5597ls0/rtss_hp
  build-types:
    - type: Debug
      west-defs:
        - CONFIG_DEBUG: y
        - CONFIG_DEBUG_THREAD_INFO: y
        - CONFIG_DEBUG_OPTIMIZATIONS: y
    - type: Release
      west-defs:
        - CONFIG_SPEED_OPTIMIZATIONS: y
  projects:
    - west:
        project-id: Zephyr_app
        app-path: .
        board: $west-board$
        west-defs:
          - DTC_OVERLAY_FILE: app.overlay
          - EXTRA_ZEPHYR_MODULES: ${SOLUTION_ROOT}/../../../executorch_matthias/pack-output/PyTorch.ExecuTorch.1.6.0
~~~

`west-defs` passes these `Kconfig` and `CMake` values into the `west` build.
`${SOLUTION_ROOT}` resolves the pack path in the generated CMake and avoids a
literal Windows drive colon, which the `west-defs` schema does not accept.
`project-id` also identifies the image context in the target set. Keep the
checked-in solution's pack, debugger, image, MLOps, and compiler entries for
this Alif E8 example.

Set the environment variables in the checked-in
[`.vscode/settings.json`](.vscode/settings.json) to these paths so the
`CMSIS Solution` extension finds Zephyr and the shared Python environment:

~~~json
{
    "cmsis-csolution.environmentVariables": {
        "ZEPHYR_BASE": "$HOME/zephyr-repo/zephyr",
        "VIRTUAL_ENV": "$HOME/zephyr-repo/.venv",
        "PATH": "$HOME/zephyr-repo/.venv/bin"
    }
}
~~~

Update these paths if your workspace is elsewhere. Open `applications/my_app`
in `VS Code`, open `zephyr.csolution.yml` with the `CMSIS Solution` extension,
select `DevKit-E8`, and run solution setup. Setup produces
`zephyr.cbuild-mlops.yml` from the solution's `mlops` settings. The checked-in
file shows the expected format; setup refreshes it for your installed tools.

Build the `DevKit-E8` target with the `CMSIS Solution` extension using the
existing `ai_layer`. The external module does not provide a `west ailayer`
command. Do not regenerate the layer as part of this setup; updating
`create_ai_layer` is a separate follow-up.

## 4. Alif E8 application

This application runs on `M55_HP` and queries the E8 general-purpose NPU-HG
(Ethos-U85, 256 MACs per cycle). `app.overlay` describes its register window
and interrupt; `prj.conf` enables the Zephyr integration and U85 core driver.

`main()` checks `device_is_ready()`, reserves the initialized driver, and calls
`ethosu_get_hw_info()` to read the hardware ID and configuration. It prints
the driver version, hardware architecture/revision, MAC count, command
stream version, and custom DMA setting. It then releases the driver and
calls `executorch_run_test()` through its C interface.

`src/executorch_test.cpp` loads the model embedded in `ai_layer/model_pte.c`
with the application-local `EmbeddedModule` in `src/arm_embedded_module.cpp`,
supplies a deterministic 16x16 RGB float ramp, and runs
`forward()`. Input shape and dimension order come from model metadata. The
Ethos-U backend executes delegated work through Zephyr's initialized
driver. The runner prints ten float logits and `Test_result: PASS` or
`Test_result: FAIL`. This is an execution smoke test; it does not compare the
logits with a numerical reference.

`ai_layer/components.conf` enables the CMSIS-ExecuTorch runtime and Ethos-U
backend from the external pack. `ai_layer/operators.conf`
selects the operators needed by the model. The root `CMakeLists.txt`
merges both fragments with `prj.conf` before
`find_package(Zephyr)`, then adds `ai_layer` after the application target exists.
`ai_layer/CMakeLists.txt` adds the embedded model and its include directory
privately to `app`, so the model is compiled once. The current model targets
`ethos-u85-256`. Keep its C array and operator selections together when the
AI layer creation workflow is updated. The application sets `ET_LOG_ENABLED=0`
for the pack runtime and its own sources in `CMakeLists.txt`.

The method allocator uses a 128 KiB pool in default RAM. A 64 KiB temporary
pool is placed in devicetree `SRAM0` so NPU scratch uses system SRAM instead of
CPU-local DTCM; the runner clears this `NOLOAD` pool. `prj.conf` also sets a
16 KiB main stack and kernel heap, cache maintenance, and float `printf`.

### Running on the E8

Use `CMSIS Load`, then start or reset the HP application. The image retains
the `0x80200000` link address used by the board's HP boot configuration. The
solution uses `debugger.load-setup.post-reset: system`; with this board, that
avoids a transient HardFault seen with the default hardware post-reset.
`CMSIS Load+Run` may show two startup sequences because it resets after
loading and again when starting Run.

The board's boot firmware must have enabled NPU-HG power, clock, and
security access. Read the `UART4` console at 115200 baud, 8 data bits, no
parity, one stop bit. Successful output includes:

~~~text
Ethos-U85 (NPU-HG) driver communication check
Device: npu@49042000, base: 0x49042000, IRQ: 366
...
MACs per cycle: 256
Ethos-U85 driver communication OK (ID/configuration read; no inference).
ExecuTorch Ethos-U85-256 application: 8832 byte model
Input shape: [1, 3, 16, 16]
...
Output: 10 element(s): 0.0079 0.0459 0.0475 -0.0475 0.0791 0.0411 -0.0285 -0.0744 -0.2246 -0.0016
Test_result: PASS
~~~

The pass marker means model loading and inference succeeded and the output
had the expected type and element count. The example runs once per boot.
