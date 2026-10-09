# Unhygienix dependency, build and flash tooling

Unhygienix means "The village fishmonger." The independent hardware target is
`unhygienix` (`CONFIG_BOARD_UNHYGIENIX`), with `PLATFORM_GABBRO` apps and the
existing Round 2/Getafix DVT2 companion identity. Getafix DVT/DVT2 hardware
configuration is separate.

Run commands from the PebbleOS repository root. Keep supplied build inputs and
generated validation artifacts under `build/`.

## Dependency setup

```sh
git submodule update --init third_party/hal_tuya/TuyaOpen-T5AI
pbl configure --board unhygienix
pbl build
```

Use the normal PebbleOS SDK/toolchain setup and Python requirements. Native AP
builds do not compile or link the Tuya application, vendor RTOS or vendor BLE
host. Native board, SoC and drivers retain ownership of the AP implementation.
The platform submodule pins `13379b63e07e78770fb4d0bffe36db2754658132`.
TuyaOpen pins `e82840dc9b629cb77fa922fb04e3f820dc2e78b4` and is optional for AP/CP
packaging. Neither submodule is upgraded implicitly.

## CP packaging

The stock CP is a generated vendor artifact, not tracked source in either SDK.
Supply the validated stock `app.bin` with SHA-256
`0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856`.
A fresh checkout needs this artifact separately to package CP. Keep the original
for rollback; unknown input images are rejected before compilation.

```sh
pbl configure --board unhygienix -DT5AI_CP_STOCK="$PWD/build/inputs/stock-cp.bin"
pbl build t5ai_cp
```

This uses the configured ARM compiler, builds the board-local RF hook, and emits
`build/pebbleos.t5ai-cp.bin`, `pebbleos.t5ai-cp-crc.bin`, and
`pebbleos.t5ai-cp.json`. The manifest records SDK revisions, stock and hook
hashes, output hashes and the supported Unhygienix mailbox/HCI ABI. It describes a
source-hook integration into a pinned binary, not a full CP source rebuild.
Changes to CP ABI, addresses or radio policy require revalidation.

## Native flashing

The tyutool runner uses the Tuya UART boot protocol; generic OpenOCD/PULSE
programming is not established on this hardware. The validated backend is
`tyutool_cli` 3.2.9; install it independently and pass its executable path.
Serial ports are explicit; prefer the stable programming UART `if00` by-id path.

```sh
pbl flash --dry-run --tyutool tyutool_cli --tyutool-tty /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00
```

Remove `--dry-run` to write. Default is AP firmware only. `--resources` includes
system resources. `--t5ai-provision --resources` also formats PFS, destroying
stored apps/data. `--t5ai-cp` includes CP from the build directory after vector,
CRC, size and manifest validation. All selected inputs are checked before the
first write. CP is written first if selected; failed writes stop the sequence.
Multiple partition writes are not atomic. Factory bootloader, configuration,
RF calibration and shared pairing storage are excluded from these operations.
Erase, recovery and OTA remain disabled. Existing dedicated guard scripts remain
usable for validation and rollback.

## Optional vendor reference build

```sh
git submodule update --init --recursive third_party/hal_tuya/TuyaOpen
python tools/build_tuya_hardware.py --python .venv-tuya/bin/python
```

Use the pinned TuyaOpen `pyproject.toml` and `uv.lock` for its separate Python
build environment. The builder stages both SDKs under `build/unhygienix_hw/sdk`
and invokes the vendor generator there, keeping submodule source pristine.
Vendor toolchain downloads remain governed by the pinned T5AI scripts. This
reference target is optional; it does not build the native PebbleOS kernel or
flash hardware. The retired vendor MPU/kernel bridge experiment is available
in Git history; the native PebbleOS kernel does not use those patches.

## Validation

Check native AP builds and existing image/map tests, then runner tests for exact
write ranges, default PFS preservation, explicit provisioning and rejection of
corrupt AP/CP inputs before any write. Hardware qualification still requires the
watchface regression, 25 reconnect cycles, cold-boot bond/face restoration and an
overnight connected-idle soak. Preserve the complete first failure trace.

## Task-state recorder

`CONFIG_T5AI_STATE_TRACE` is an optional compile-time diagnostic flag, disabled
by default. Enable it explicitly for an investigation:

```sh
pbl configure --board unhygienix -DCONFIG_T5AI_STATE_TRACE=y
pbl build
```

Disable it again in the same build directory:

```sh
pbl configure --board unhygienix -DCONFIG_T5AI_STATE_TRACE=n
pbl build
```

CMake equivalents are `cmake -S . -B build -DBOARD=unhygienix
-DCONFIG_T5AI_STATE_TRACE=y` (or `n`) followed by `cmake --build build`.
The override persists in that build directory's CMake cache. Check `.config`
for `CONFIG_T5AI_STATE_TRACE=y` or `# CONFIG_T5AI_STATE_TRACE is not set`.
Rebuild and flash the resulting firmware to change the watch's behavior;
there is no runtime toggle.

When disabled, CMake excludes `soc/t5ai/state_trace.c` and the start/freeze
hooks compile to empty inline functions. There is no recorder thread, sampling,
retained ring or `T5STATE` output. When enabled, the recorder adds a 1536-byte
thread stack and an 848-byte retained ring, plus thread bookkeeping and code.
A dedicated thread samples KernelBG and App once per second into a 16-entry
internal-RAM ring. Sampling performs no logging, allocation, flash writes,
stack walks, or register capture. This flag does not change watchdog timing:
the board independently configures checks every 1000 ms and 2000 ms overdue
before reset; the task timeout remains 5000 ms.

The T5 watchdog reason setter and whole-chip reset path freeze the first
failure snapshot. On the next boot, valid retained records appear in the
normal UART trace with the `T5STATE` prefix. Record checksums reject torn or
corrupt samples. Retention through the vendor whole-chip boot path still
requires hardware validation; power loss can erase the ring. There is no
persistent-flash fallback.

Each sample reports uptime ticks, the background callback address, and paired
BG/APP records with thread ID, cumulative CPU ticks, state, base/effective
priority, and wait-queue address. States are 0 ready, 1 running, 2 blocked,
3 suspended, 4 dead, and 255 absent. Compare CPU counters only for the same
thread ID; app IDs change when threads are recreated. Resolve callback
addresses with the exact candidate ELF. `same_callback_ticks` measures how
long the sampled pointer stayed unchanged; repeated invocations of the same
function are indistinguishable. A zero callback means no callback was active
when sampled.

A ready KernelBG with little CPU progress while the app advances supports
starvation. A consistently blocked KernelBG and stable wait queue supports a
wait on an object. Increasing KernelBG CPU time indicates continued execution
rather than complete starvation. The ring freezes on the first watchdog
report even if throttling later recovers; reboot starts a fresh recording.

### QMI8658 polling isolation

Periodic accelerometer I2C acquisition runs on the static `QMI8658` worker
(priority 2, 1536-byte stack). NewTimers only signals a binary semaphore;
KernelBG only delivers the acquired sample and runs the existing shake filter.
The sensor ODRs, requested sampling intervals, startup settling and axis mapping
are unchanged. Configuration and explicit uncached `accel_peek()` calls remain
synchronous. A driver-local mutex serializes register-pointer/read pairs with
configuration writes.

Only one acquisition or delivery is outstanding. Ticks during that work are
dropped, and delivery uses the nonblocking, droppable system-task API. A full
KernelBG queue drops that sample and allows the next tick to retry; no catch-up
burst is generated. Power, rotation and rate changes invalidate stale samples.
This isolates periodic I2C waits from KernelBG; it does not establish the cause
of the previously observed stalls or prove hardware stability. Validate with
MU-TH-UR T app loading and a sustained accelerometer subscription after flashing.

`python3 -m unittest tools.tests.test_t5ai_qmi8658 -v` compiles the production
driver with controlled I2C and scheduler stubs. It checks pending-work bounds,
queue pressure, read failures, stale samples, startup settling, and that neither
the timer nor the delivery callback performs I2C.

### Synchronous flash erase completion

T5 CP flash erase IPC completes synchronously: all requested 4 KiB erases
finish before the T5 driver's `erase_*_begin()` returns. The stock flash API
still polls once to consume the completion, release its erase semaphore and
deliver callbacks. The T5 driver therefore advertises a 2 ms completion-poll
duration, which the shared API rounds to a 1 ms first poll. This is a scheduling
hint after synchronous completion, not a claim about physical erase time.
Do not set it to zero or one: the shared API would skip polling and strand the
completion.

The previous 50/800 ms hints inserted 43/700 ms sleeps after already-completed
erases. Settings/database rewrites kept their mutex held through those sleeps.
The driver-local change removes that avoidable delay without changing database
locking, rewrite atomicity, watchdog limits or the CP flash protocol. Actual
flash work, settings scans and scheduling delays can still extend mutex holds;
compare the `FIRM-1649` hold-time logs across a phone full sync after flashing.

Host validation: `python3 -m unittest tools.tests.test_t5ai_flash_completion -v`
checks the production T5 driver; native `test_flash_api` covers both blocking
completion and asynchronous callbacks with the short polling hint.
