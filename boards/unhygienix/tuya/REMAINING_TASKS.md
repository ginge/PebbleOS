# Unhygienix validation and remaining work

The [operator handoff](../../../docs/boards/unhygienix/index.md) records current hardware
support and implementation gaps. The [tooling guide](../../../docs/boards/unhygienix/tooling.md)
records dependency, build, packaging and flash procedures.

1. Verify watchface install/launch and selection persistence.
2. Run 25 disconnect/reconnect cycles, retaining the first complete failure trace.
3. Cold boot and verify bond restoration, protocol reconnection and selected face.
4. Run overnight connected-idle with periodic UI interaction.
5. Resolve App privileged-code stalls and resource/app-entry warnings.
6. Complete hardware wake/alarm routing and low-power operation.
7. Validate battery/charging with a fitted cell, RTC drift, privilege isolation,
   storage interruption and corruption recovery.
8. Define rollback and recovery before enabling firmware updates or OTA.

Keep captures and matching firmware/log dictionaries under `build/traces/`.
Host builds do not establish physical qualification. Confirm actual received
bytes before reporting a capture or soak as active.

## Deferred KernelBG stall investigation — 2026-10-09

Deferred at the operator's request. MU-TH-UR T loading still triggers a
KernelBG watchdog stall after the synchronous-erase delay fix. In the latest
flashed candidate, the task was unfed for 5435/6442 ms, then recovered after app
throttling without a reboot during the bounded observation. App-driven CPU
starvation is a hypothesis, not an established root cause; communications or
lock waits remain possible.

When resumed, correlate KernelBG/App CPU-time deltas and ready/blocked state
with the active communications endpoint, callback elapsed time, settings
filename/progress and send-buffer wait duration. Use the optional
`CONFIG_T5AI_STATE_TRACE` recorder if useful; it remains disabled by default.
Do not add stack/register tracing or change shared scheduling policy merely
from sampled PCs. Preserve exact firmware, dictionary and first-failure logs.
Evidence: `build/traces/erase-delay-fix-20261009/flash-results.json` and
`build/traces/erase-delay-fix-flash-20261009.log`.

## Remaining stock-code refactor work

The historical [shared-code audit](../../../docs/boards/unhygienix/history/shared_code_cleanup.md)
contains superseded startup/kernel work. The remaining candidates are:

- Extract the single-bank system-resource policy and bounded metadata validator
  from shared `resource_storage_flash.c` into a selected T5 backend. Preserve
  fast boot validation, driver-local mapped reads and resource backend ordering.
- Resolve resource-watch callback ownership before restoring stock
  `resource_unwatch()` behavior; do not simply remove its T5 guard.
- Select unavailable update, recovery, coredump and reset implementations or
  explicit capabilities at build time, reducing shared T5 conditionals without
  enabling unsupported operations.
- Finish compositor framebuffer-placement ownership. PSRAM validation and power
  setup have moved to the board; the shared framebuffer section annotation
  remains. Earlier provider extraction faulted and needs a sound new design.
- Audit remaining shared identity/profiler exceptions and CMake selection glue;
  refresh stale board tests after the startup/service extractions. Separate
  independently justified generic fixes from platform policy.

Stock `main.c`, native kernel wrappers/startup, SoC-local delay selection and
most common service restoration are already completed; do not repeat those
extractions. Hardware qualification and remaining peripheral implementations
are separate work from this refactor list.
