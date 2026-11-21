# Unhygienix / Tuya code-quality TODO

Created 21 November 2026 from the static review of `tuya` at `8ea548d80`
against local `origin/main`. This is the top-priority engineering cleanup
backlog, linked from the existing [power TODO](power-todo.md) and
[remaining-work list](../../../boards/unhygienix/tuya/REMAINING_TASKS.md).

The review covered the board, SoC, drivers, CP integration, tooling, tests and
shared-code changes. Vendor interfaces relevant to the port were examined;
this is not an exhaustive line-by-line audit of every vendor SDK file.
Runtime risks below are static findings, not newly reproduced hardware faults.

## Execution order and constraints

Repair the test baseline first, then bound console work, hardware waits and
HCI retries. Replace touch/PFS interception next, followed by sleep, build and
driver cleanup. Each numbered finding below is a separate reviewable change;
combine changes only when they have a necessary dependency.

Make production changes in `/home/baz/coredevices/pebbleos`. Preserve stock
shared/common code wherever a board-, SoC- or driver-local solution exists.
A shared extension requires a specific justification and regression coverage
for existing targets. Preserve the pinned AP/CP ABI, numeric protocol values,
image/partition/retained layouts, hardware timing, calibration, Bluetooth
identity, Gabbro application ABI, PFS/resources/bonds and ownership/quarantine
rules. Cleanup does not authorize CP deep sleep, altered release policy or
promotion of an unqualified image. Existing hardware release blockers remain.

## High priority

### 1. Test harness fidelity and broken resource fixtures

- [x] Fix the four resource-backend harness compilation errors caused by
  missing production partition/configuration definitions in
  `tools/tests/test_t5ai_resources.py`.
- [x] Correct `tools/tests/native_t5ai_harness.py`'s 32,768 Hz AON definition
  to use the production 32,000 Hz definition from the real header.
- [x] Prefer complete production translation units and real public types and
  constants over stripped includes, extracted functions and recreated layouts.
  Retain text assertions only for deliberate build/ABI constraints.
- [x] Acceptance: resource cases compile and pass, timing tests use production
  constants, and the baseline clearly separates missing tools from failures.

### 2. Bound CP console draining

- [x] In `drivers/uart/t5ai_console.c`, replace draining the live ring until
  empty with a byte/time budget per work invocation; resubmit pending work.
- [x] Count overflow drops and failed CP sends; preserve mailbox-owner
  exclusion and define bounded panic-drain behavior.
- [x] Acceptance: continuous producers cannot starve an unrelated system-work
  item; ring ordering, wrap, failure and panic paths remain covered.

### 3. Bound hardware register waits

- [x] Add bounded FIFO-ready/FIFO-empty waits in `drivers/uart/t5ai.c` and a
  bounded timer-latch wait in `soc/t5ai/delay.c`.
- [x] Use an independent time source with an iteration fallback where needed.
  Define failure behavior, restore interrupt state, and retain clocks/blockers
  when completion is uncertain.
- [x] Acceptance: stuck register bits terminate safely, including the
  interrupt-disabled latch path; normal wire timing remains unchanged.

### 4. Bound HCI ACL packet retries

- [x] In `third_party/nimble/transport/hci_t5ai.c`, classify retryable
  backpressure versus terminal errors and add an absolute packet deadline to
  `prv_send_queued_acl()`.
- [x] Rate-limit retry warnings and schedule host recovery on expiry while
  preserving packet ordering, buffer ownership and descriptor quarantine.
- [x] Acceptance: persistent controller rejection cannot hold a packet forever;
  transient backpressure, recovery and late ACK cases pass.

### 5. Replace touch queue-internals interception

- [ ] Replace spare-event-byte timestamps and direct message-queue backing
  storage edits in `soc/t5ai/touch_queue.{c,h}` with explicitly owned,
  timestamped samples in a bounded input buffer and a defined delivery boundary.
- [ ] Preserve touchdown/liftoff and navigation barriers, the latest two
  velocity samples, acquisition time and the application `TouchEvent` ABI.
  Justify any unavoidable narrow shared extension before implementing it.
- [ ] Acceptance: burst, wrap, app-switch, queue-pressure and gesture-velocity
  regressions pass without depending on private queue fields.

### 6. Replace SD/PFS allocation-order and linker hooks

- [ ] Replace `soc/t5ai/sd_pfs.c`'s next-allocation page-flags interception
  with a specific buffer-provider boundary if needed.
- [ ] Select a board-local FTL implementation explicitly at build time rather
  than relying on `__wrap_ftl_*`; preserve existing FTL signatures/NOR semantics.
- [ ] Acceptance: volume geometry/header, provisioning guard, reopen, format,
  partial/multi-sector I/O, failure handling and full-volume behavior remain
  correct. Keep the existing fail-stop policy until error propagation is designed.

## Medium priority

### 7. Make production build integration explicit

- [ ] Separate diagnostic instrumentation from production integration in
  `soc/t5ai/CMakeLists.txt`; inventory renames, source suppression, wrappers and
  source-specific compilation changes.
- [ ] Prefer explicit backend selection and a few documented extension points.
  Remove empty CPU-metrics deferred-function scaffolding.
- [ ] Acceptance: release/LTO links select exactly the intended implementations;
  retained interception has a documented purpose and link-level verification.

### 8. Separate qualified sleep from experimental modes

- [ ] Isolate the qualified CP-awake production path in `soc/t5ai/sleep.c`,
  `boards/unhygienix/tuya/cp/native_alarm.c` and `sleep_protocol.h` from the
  accumulated CP transition investigations.
- [ ] Replace interdependent experimental mode booleans with named policies and
  explicit transitions. Preserve numeric ABI values; reject unsupported modes.
- [ ] Acceptance: cancellation, late replies, cleanup and production state
  transitions pass; no CP low-voltage behavior is enabled by cleanup.

### 9. Make HCI wakeups event-driven

- [ ] Map every producer of HCI work, then use existing notifications as the
  normal wake mechanism in all configurations.
- [ ] Replace periodic 5/100 ms polling with waits timed to actual retry,
  receive-retention and recovery deadlines.
- [ ] Acceptance: lost-wakeup/race tests pass; compare idle wakeups and packet
  latency before changing qualified defaults.

### 10. Give graphics optimizations explicit boundaries

- [ ] Benchmark `soc/t5ai/compositor_fast.c`, `pixel_fast.c` and
  `text_node_cache.c` independently; retain measured benefits.
- [ ] Replace hidden stock-function renaming with explicit eligibility/entry
  points where feasible. Derive geometry from bitmap/board configuration and
  assess the compositor's row-extent scanning cost.
- [ ] Acceptance: differential stock comparisons cover clipping, blending,
  circular rows, unusual layouts and text-cache freshness; performance is measured.

### 11. Make PSRAM pool placement robust to upstream renames

- [ ] Replace `soc/t5ai/framebuffer.ld`'s capture of NimBLE private `.bss`
  symbol names with explicit storage sections or supported pool backing storage.
- [ ] Until then, make map validation assert expected objects, sizes and ranges.
- [ ] Acceptance: upstream symbol/size drift fails visibly instead of silently
  moving pool capacity into scarce internal SRAM.

### 12. Move singleton driver configuration into native devices

- [ ] In `drivers/panel/co5300.c` and the CST92xx touch driver, obtain the
  actual device instance from the class argument and store mutable state in it.
- [ ] Move pins, bus references, orientation and dimensions to board device
  configuration instead of global objects and hardcoded driver constants.
- [ ] Acceptance: existing board behavior and initialization order are preserved;
  driver behavior can be exercised with a separately configured test instance.

### 13. Use scheduler sleeps for long panel delays

- [ ] Establish calling-context requirements for CO5300 reset/sleep sequences,
  then replace 120–200 ms busy waits with native thread sleeps in scheduler context.
- [ ] Keep short timing delays and any necessary early-boot fallback.
- [ ] Acceptance: minimum timings and command/error sequences remain correct;
  startup/recovery responsiveness is measured.

### 14. Schedule battery work from events and deadlines

- [ ] Replace the permanent one-second servicing poll in
  `drivers/battery/t5ai.c` with charge-edge and ADC-completion work, plus delayed
  debounce/retry deadlines.
- [ ] Retain slower missed-edge reconciliation if required, asynchronous
  measurement semantics and invalid-reading protection.
- [ ] Acceptance: charge bounce, missed edges, ADC errors/retries and startup
  behavior pass; idle wakeups decrease without delayed state delivery.

### 15. Separate flash and ADC initialization ownership

- [ ] Remove SARADC initialization ownership from `t5ai_flash_ipc_init()`.
  Keep shared MB-IPC initialization in the transport and make flash/SARADC
  explicit, idempotent clients with any required socket order documented.
- [ ] Acceptance: either client's failure/retry is independently covered and
  storage startup does not depend unnecessarily on ADC setup.

### 16. Track logging recursion by actual thread identity

- [ ] Review unregistered native logging contexts: all currently share the
  `PebbleTask_Unknown` flag in `pebble/kernel/kernel_applib_state.c`.
- [ ] Route ordinary port diagnostics through registered work queues where
  appropriate; use actual thread identity if independent native logging is needed.
- [ ] Acceptance: two distinct unknown threads do not appear to be recursive
  logging, while same-thread recursion and mailbox-owner exclusion stay safe.

### 17. Replace shared SoC policy checks with precise capabilities

- [ ] Use the recovery/install capability for factory-reset recovery operations
  instead of `SOC_T5AI`, after checking the exact semantics.
- [ ] Move Unhygienix pairing button assignments into board configuration with
  stock defaults, rather than naming the board in shared pairing UI code.
- [ ] Acceptance: other boards retain their reset and pairing behavior; the
  Unhygienix unsupported-install guards remain effective.

## Lower priority

### 18. Consolidate retained-storage interfaces

- [ ] Choose one existing retained-word interface instead of overlapping
  `retained_storage_*`, `retained_read/write`, T5AI and RTC backup adapters.
- [ ] Implement platform behavior locally and keep transactional clock commits
  separate from ordinary word access.
- [ ] Acceptance: layout, checksum, reboot reasons, interrupted commit handling
  and old-image compatibility remain intact.

### 19. Make CP contracts and overlays readable

- [ ] Give `tools/unhygienix_cp_build.py`'s positional contract named fields
  and generate duplicated protocol constants from one definition.
- [ ] Store substantial overlay additions as readable source or reviewable
  patches instead of escaped replacement strings.
- [ ] Acceptance: input hashes, replacement-count checks, relocation audits and
  exact ABI verification remain; tooling refactors do not change CP behavior.

### 20. Clarify checksum and reference-source boundaries

- [ ] Evaluate native CRC primitives through adapters proven against vendor
  vectors; remove the duplicate flash CRC table only after equivalence and
  performance checks. Preserve unusual SARADC/MIPC checksum semantics explicitly.
- [ ] Document the optional Tuya application as a separate reference and add
  comparison checks for pins, orientation, geometry and calibration.
- [ ] Acceptance: checksum vectors remain byte-identical; configuration
  discrepancies are resolved using hardware evidence before either side changes.

## Changes to retain unless new evidence warrants revision

Retain native SPI lane support, RGB565 panel format, battery invalid-measurement
handling and generic update-capability gates. Do not simplify the MPU restore
workaround without architectural evidence. Keep shared Bluetooth fixes separate
from board cleanup and validate them independently.

## Review validation baseline

No production source or hardware was changed during the review. Sixteen targeted
tests passed. A broader rerun with LeakSanitizer leak detection disabled for the
sandbox exposed four resource-harness compilation errors. Dependency-related
modules were rerun separately: 43 tests completed successfully, with 10 skipped.
These runs are not a clean full-suite result or hardware qualification.

Session-local logs were `/tmp/pebble-port-audit-tests-venv.log` and
`/tmp/pebble-port-audit-dependency-tests.log`; they are ephemeral evidence, not
repository artifacts. Hardware behavior, power and soak stability remain untested
by this audit. Record durable results alongside each completed TODO item.

## Implementation progress — 21 November 2026

Started on `tuya` at `1eea7444d`. Initial local changes were the two planning
links in `power-todo.md` and `REMAINING_TASKS.md`, plus this untracked backlog.
Those planning changes were preserved. No reference-workspace source or shared
Pebble production code was changed. No hardware operations were performed.

### Increment 1 — resource baseline and clock definition

Reproduced all four resource compilation failures on unchanged HEAD with
`ASAN_OPTIONS=detect_leaks=0 python3 -m unittest discover -s tools/tests -p
 'test_t5ai_resources.py' -v`: four errors, stock case passed. The fixture omitted
firmware/config/crash-partition definitions needed by existing static guards.

The resource tests now compile complete backend, shared routing and CRC units
against real flash types, image/partition/compiler headers, native POSIX thread
layout and board Kconfig values. Geometry overrides are limited to truncated
bank cases and the stock backend's deliberately synthetic spare bank. Production
layout assertions remain enabled. The native display prefix includes the real
RTC header (32,000 Hz), with no copied AON frequency. Existing older harnesses
elsewhere still strip includes/extract functions; migration is incremental,
not a claim that all host fixtures now model complete production integration.

Validation: temporary `/tmp/pebble-quality-venv` with `kconfiglib`, `pyelftools`
and `PyYAML`; `ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python
-m unittest tools.tests.test_t5ai_resources tools.tests.test_t5ai_display_class
tools.tests.test_t5ai_display_rotation tools.tests.test_t5ai_display_batch
tools.tests.test_t5ai_display_power tools.tests.test_t5ai_display_recovery
tools.tests.test_t5ai_display_worker -v`: **13 passed**.

Broad discovery using the same interpreter and ASAN setting:
`python -m unittest discover -s tools/tests -v`: **431 tests, 5 import errors,
16 skipped**. Every loaded test passed. Import errors were missing `numpy`
(fuel-gauge modules), `libpebble2`, `freetype`, and `svg`; skips include absent
CMake, Unicorn and CP artifact sets. These are environment/tooling limitations,
not production regressions. This is not a clean full-suite or firmware build.
Session logs in `/tmp/pebble-quality-{step1,baseline-deps}.log` are ephemeral;
the commands and counts here are the durable record.

### Increment 2 — bounded console work

Each work invocation sends at most 636 bytes (four full CP log packets), then
resubmits remaining work at the queue tail. Panic sends at most one ring
capacity (1,024 bytes), without its own resubmission. A drain ownership guard
prevents concurrent/reentrant drain interleaving. Mailbox-owner exclusion is
preserved. Debugger-visible overflow-byte and failed-send counters avoid
recursive diagnostics; failed chunks are never replayed after possible publication.
The bound limits work per invocation; it does not add a timeout to the existing
mailbox logging mutex or change CP mailbox ownership/completion rules.

`ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest
 tools.tests.test_t5ai_console -v`: **passed**, with ASan/UBSan, a complete
production console unit and real UART/work/mailbox types. Covers FIFO endpoint
fairness with continuous production and unrelated work, wrap/order, overflow,
failed sends, owner exclusion, unsafe panic contexts and reentrant panic.
The FIFO scheduler endpoint models the documented native work-queue contract;
this is host validation, not a watch scheduling/latency measurement.

### Increment 3 — bounded hardware waits

UART FIFO-ready/empty waits now have a one-second ceiling; the existing
100 ms final-stop-bit bound remains. The production AON low counter supplies
an independent deadline (wrap-safe), with finite iterations if AON stops.
The one-microsecond polling delay preserves existing gated-UART timing.
Failed output drops the unsent byte through the existing void UART API;
failed completion retains clocks and sleep ownership until completion can be
proven. Gating drain faults remain sticky until explicit peripheral reinit.

Timer latch requests terminate after one millisecond on AON, or 65,536 polls
if AON stops. The original interrupt mask is restored before an always-enabled
fatal assertion. A successfully latched but frozen timer is also bounded.
The fatal policy avoids returning a short delay to timing-sensitive hardware;
normal 26 MHz timer scaling and setup registers are unchanged.

`ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest
 tools.tests.test_t5ai_uart tools.tests.test_t5ai_uart_idle_clock
 tools.tests.test_t5ai_delay tools.tests.test_t5ai_console -v`: **4 passed**,
including full-unit ASan/UBSan UART/delay tests, stuck ready/empty/latch/count,
live/stopped/wrapping AON, normal timer wrap/minimum delay, both interrupt masks,
late IRQ, last-bit timeout, clock quarantine and retained blockers.
No hardware wire-timing qualification is implied.

Tooling: system formatters 18/21 reject the repository's newer configuration.
Installed `clang-format` 23.1.5 and Ruff in the temporary environment instead;
no repository formatting configuration was changed. Changed C hunks pass
`git-clang-format-21 --binary /tmp/pebble-quality-venv/bin/clang-format --diff
 HEAD -- drivers/uart/t5ai_console.c drivers/uart/t5ai.c soc/t5ai/delay.c
 third_party/nimble/transport/hci_t5ai.c`. All seven changed/new Python files
pass Ruff check and format check; `git diff --check` passes.

### Increment 4 — bounded ACL packet retries

The transport retries CP rejection (`E_ERROR`, translated from a nonzero
exchange result) and unpublished mailbox backpressure (`E_BUSY`). Ambiguous
IPC timeouts, exhaustion, invalid arguments/ranges and faulted transport state
schedule host reset immediately. The packet has one 2,500 ms retry deadline,
using the real 64-bit native uptime type; a retry or scheduler delay cannot
restart it. Warnings emit on the first rejection and at most every 1,000 ms.
No new attempt starts after expiry. The existing synchronous IPC call is not
preempted: an in-flight exchange or mutex wait can extend wall time past the
retry deadline. This change bounds the previously endless sequence of attempts;
it does not replace the lower layer's existing per-exchange bounds/locking.

Recovery preserves queue ordering, consumes old-session TX under the existing
reset policy and leaves the static packet staging bytes intact for possible
late ACKs. No lower-layer descriptor reuse/quarantine or AP/CP ABI was changed.

`ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest
 tools.tests.test_t5ai_hci_acl tools.tests.test_t5ai_hci_pressure
 tools.tests.test_t5ai_hci_sleep tools.tests.test_unhygienix_cp_mailbox_lifetime
 -v`: **7 passed**. The new test compiles the complete transport, real NimBLE
mbuf implementation and native tick-conversion unit with real headers and board
Kconfig. It covers fragmented buffer copying/freeing, rejection/backpressure,
terminal statuses, stopped transport, scheduler/IPC delays, malformed packets,
long uptime across and beyond 32-bit wrap, warning limits and stale-session
buffer ownership. Existing receive-pressure/recovery and delayed-ACK quarantine
regressions pass; their older extracted-function fixtures remain a limitation.

### Combined verification and remaining boundary

Final broad discovery with the temporary interpreter and
`ASAN_OPTIONS=detect_leaks=0`: **433 tests: 412 passed, 16 skipped, 5 dependency
import errors**, the same unrelated missing modules listed under increment 1.
Log: `/tmp/pebble-quality-final.log` (ephemeral). After the final resource
mapping derivation, the focused resource/console/UART/delay/ACL run passed all
**10 tests**. The final UART fault-reporting deduplication was then validated
by the UART clock/retained test and ARM compilation.

All four changed production units compile individually with GCC 14.2.1 for
Cortex-M33, using the real ARM/CMSIS/Unhygienix/NimBLE headers and generated
board configuration. UART also compiles with idle gating, retained sleep,
both together, and the sleep guard. These are object-compilation checks, not
a full firmware/release/LTO link or hardware qualification. Reproduce the base
configuration with:

```sh
PYTHONPATH=. /tmp/pebble-quality-venv/bin/python -c 'from tools.tests.test_t5ai_config import load_config; load_config("unhygienix").write_autoconf("/tmp/pebble-quality-arm/autoconf.h")'
```

Compile each of `drivers/uart/t5ai_console.c`, `drivers/uart/t5ai.c`,
`soc/t5ai/delay.c`, `third_party/nimble/transport/hci_t5ai.c` with:

```sh
/home/baz/pebble/build/arm_gnu_toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc \
  -std=gnu23 -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
  -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
  -include /tmp/pebble-quality-arm/autoconf.h \
  -Isoc/t5ai -Iinclude -Iinclude/pbl -Ipebble -Ikernel/arch/arm/include \
  -Ithird_party/cmsis_core/CMSIS/CMSIS/Core/Include \
  -Ithird_party/nimble/port/include \
  -Ithird_party/nimble/mynewt-nimble/porting/nimble/include \
  -Ithird_party/nimble/mynewt-nimble/nimble/include \
  -Ithird_party/nimble/mynewt-nimble/nimble/host/include \
  -Ithird_party/nimble/mynewt-nimble/nimble/transport/include \
  -c SOURCE -o /tmp/pebble-quality-arm/OBJECT.o
```

UART variants add `-DCONFIG_T5AI_UART_IDLE_GATING=1`,
`-DCONFIG_T5AI_RETAINED_SLEEP=1`, both, or `-DCONFIG_T5AI_SLEEP_GUARD_TEST=1`.
`delay_init()` precedes early console setup in the native boot sequence; the
new UART polling delay does not introduce an uninitialized-timer dependency.

**Next unresolved item: finding 5.** Reinspection confirms that the default
Unhygienix touch integration still stamps spare event-union bytes, accesses
`pbl_msgq` backing storage/count/tail directly, and wraps event put, queue
put/get and recognizer dispatch. Replacing this needs an explicitly owned
sample buffer and a boundary across acquisition, KernelMain wake/navigation
stamping, app forwarding and recognizer delivery. KernelMain mutates navigation
and task masking before forwarding, so bypassing it with direct recognizer
callbacks would change behavior. Acquisition time and the latest two velocity
samples must survive this path, app switches and queue pressure. Existing touch
fixtures duplicate public layouts and cannot establish the ABI safety of a
replacement. No touch/PFS/shared implementation was changed in these four
increments; findings 5–20 remain open. Any required shared extension must be
justified and tested for stock targets before implementation.
