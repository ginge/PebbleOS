# Unhygienix / Tuya code-quality TODO

Created 21 November 2026 from the static review of `tuya` at `8ea548d80`
against local `origin/main`. This is the top-priority engineering cleanup
backlog, linked from the existing [power TODO](power-todo.md) and
[remaining-work list](../../../boards/unhygienix/tuya/REMAINING_TASKS.md).

The review covered the board, SoC, drivers, CP integration, tooling, tests and
shared-code changes. Vendor interfaces relevant to the port were examined;
this is not an exhaustive line-by-line audit of every vendor SDK file.
The original review described static risks. The progress record below also
records subsequent hardware observations, including the 21 November timer panic.

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
  The 21 November startup false panic was repaired by retaining the completion
  observation, with unchanged deadlines and counter scaling; see the repair
  record below. Wider hardware timing/soak qualification is not claimed.

### 4. Bound HCI ACL packet retries

- [x] In `third_party/nimble/transport/hci_t5ai.c`, classify retryable
  backpressure versus terminal errors and add an absolute packet deadline to
  `prv_send_queued_acl()`.
- [x] Rate-limit retry warnings and schedule host recovery on expiry while
  preserving packet ordering, buffer ownership and descriptor quarantine.
- [x] Acceptance: persistent controller rejection cannot hold a packet forever;
  transient backpressure, recovery and late ACK cases pass.

### 5. Replace touch queue-internals interception

- [x] Replace spare-event-byte timestamps and direct message-queue backing
  storage edits in `soc/t5ai/touch_queue.{c,h}` with explicitly owned,
  timestamped samples in a bounded input buffer and a defined delivery boundary.
- [x] Preserve touchdown/liftoff and navigation barriers, the latest two
  velocity samples, acquisition time and the application `TouchEvent` ABI.
  Justify any unavoidable narrow shared extension before implementing it.
- [x] Acceptance: burst, wrap, app-switch, queue-pressure and gesture-velocity
  regressions pass without depending on private queue fields.

### 6. Replace SD/PFS allocation-order and linker hooks

- [x] Replace `soc/t5ai/sd_pfs.c`'s next-allocation page-flags interception
  with a specific buffer-provider boundary if needed.
- [x] Select a board-local FTL implementation explicitly at build time rather
  than relying on `__wrap_ftl_*`; preserve existing FTL signatures/NOR semantics.
- [x] Acceptance: volume geometry/header, provisioning guard, reopen, format,
  partial/multi-sector I/O, failure handling and full-volume behavior remain
  correct. Keep the existing fail-stop policy until error propagation is designed.

The complete-unit check also reproduced a migrated format path reaching NOR
under the SD configuration. Whole-volume erase now follows the selected FTL;
see the 22 November progress record. Validation is host/build evidence, not
on-watch format or storage qualification.

### Follow-up: repair post-flash sleep admission (release blocker)

- [x] Reproduce and explain first-boot `T5SLEEP failed reason=4 gen=0`
  with AP/CP reset sources `0x4f/0`, preserving the original failure traces.
  Reason 4 is `T5AISleepFault_ResumeTimeout`, not a generic budget rejection;
  inspect the reset-reason and unfinished retained-trace admission paths.
  `drivers/watchdog/t5ai.c` intentionally writes `0x4f` while arming the retained
  watchdog guard and restores the previous reason only after return. A reset
  during that interval is not automatically proof of a spurious fault.
- [x] Preserve and report previous reset/retained evidence without treating it
  as a new runtime fault in production AP-only mode. Revalidate the surviving
  CP alarm owner and awake contract before admitting a fresh attempt. The
  operator accepts a reboot if a genuine sleep/restore hang recurs.
- [x] Acceptance: first boot after an authorized raw AP-only flash admits the
  CP-awake path when safe, without a programming handshake or recovery reset.
  Current-boot invalid contracts, late replies, ownership faults and uncertain
  cleanup remain guarded. Preserve reset/retained layouts, numeric ABI,
  hardware timing and CP defaults; keep diagnostic CP-sleep modes conservative.
- [x] Record exact image/configuration and first-failure/first-boot evidence;
  do not use a subsequent manual reset as proof of a fix. Host tests alone
  cannot qualify retained sleep. Investigate after finding 6, before the
  broader sleep/architecture cleanup in findings 7–8.

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

### Increment 5 — owned touch samples and public delivery boundaries

Resumed on `tuya` at `a253674e7`. The two pre-existing planning-link edits
remain untouched. Reinspection confirmed the spare-union-byte timestamp and
private queue-storage mutation in the enabled Unhygienix path.

Replaced both with a board-owned pool of 96 timestamped `TouchEvent` samples.
The existing callback-event envelope carries a token; it never carries a
private sample pointer into an app. Tokens are expanded to ordinary touch
events at the public delivery boundary. KernelMain tokens remain envelopes
until `event_take_timeout()` finishes native callback accounting, then expand
before the unchanged launcher wake/navigation handling. Forwarded app samples
retain that acquisition timestamp and the full production `PebbleTaskBitset`.
No shared extension or change to `TouchEvent`, `PebbleEvent`, event numbers,
SDK/Gabbro ABI, native queue layout or stock code was needed.

Coalescing edits only owned pending samples. The latest two consecutive motion
samples are retained; touchdown/liftoff, navigation-state changes and unrelated
messages are barriers. A rejected transition also ends the coalescing tail.
Ownership transfers after native dequeue, under the IRQ lock; a producer racing
that transfer can update still-owned motion without editing native queue slots.
Queued samples belong to a native app thread generation. Old-generation samples
become ordinary null events when consumed by a replacement app, including an
unprivileged app. Cleanup consumers release the records normally; failed queue
publication releases its record. Duplicate/invalid tokens cannot republish an
already-owned record. Unrelated app queues do not invalidate touch timing.

The source retains public `event_put`, `event_take_timeout`, queue put/get and
recognizer wrappers, plus the existing pan clock adapter. Their purpose and
non-LTO boundaries are explicit in the SoC build file; replacing broader build
interception remains finding 7. Only the public queue configuration `msg_size`
is inspected. There is no access to queue backend fields or backing storage.
The ARM pool occupies **3,072 bytes** of `.bss`; no retained or partition layout
was changed. Full firmware linking is still required to assess total SRAM/flash
headroom. Allocation exhaustion follows the existing failure contract: the void
input event API fails stop; app publication returns an allocation/queue error,
without silently coalescing away transitions or weakening the service's guards.

Validation:

```sh
ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest \
  tools.tests.test_t5ai_touch_queue tools.tests.test_t5ai_touch -v
```

**17 passed.** The new fixtures compile the complete board transport, native
message queue, pan, recognizer, geometry and tick-conversion translation units
with real headers/types and board Kconfig. Only execution-state/time, scheduler
and public event-loop endpoints are modeled. Cases cover Main backlog, full-width
acquisition time, actual pan velocity (1,000 px/s after delayed delivery), bursts,
wrap, dequeue/producer interleaving, transition/navigation/unrelated-message
barriers, queue/pool exhaustion, failed publication, app switches, cleanup and
unprivileged fallback. No functions are extracted and no public layouts are
recreated. A second executable uses LTO across the same non-LTO public seams as
the production integration. Normal execution uses ASan/UBSan; LTO uses UBSan.
The installed LLVM gold plugin fails to link ASan constructors with LTO (discarded
constructor-section references), a tooling limitation; the normal ASan and LTO
UBSan runs both pass. Assertions and production guards remain enabled.

Existing stock suites were built and run independently, without the board
transport, using temporary tools/dependencies:

```sh
PATH=/tmp/pebble-quality-venv/bin:$PATH cmake -S tests \
  -B /tmp/pebble-quality-stock-tests -G Ninja \
  -DCMAKE_C_COMPILER=clang-21 -DPBL_TEST_IMAGES=OFF \
  -DPython3_EXECUTABLE=/tmp/pebble-quality-venv/bin/python
PATH=/tmp/pebble-quality-venv/bin:$PATH CCACHE_DISABLE=1 cmake --build \
  /tmp/pebble-quality-stock-tests --target test_pan test_recognizer_manager \
  test_recognizer_competition test_touch_nav test_touch_service -j 4
/tmp/pebble-quality-venv/bin/ctest --test-dir /tmp/pebble-quality-stock-tests \
  --output-on-failure \
  -R '(test_pan|test_touch_service|test_touch_nav|test_recognizer_manager|test_recognizer_competition)$'
```

**All five stock suite executables passed** (Emery configuration). Initial build
failures were missing nanopb/protoc and a read-only ccache directory; temporary
`nanopb`, `grpcio-tools`, CMake/Ninja and `CCACHE_DISABLE=1` resolved them without
source changes. These were tooling failures, not touch regressions. Targeted
logs are `/tmp/pebble-quality-touch-final.log`,
`/tmp/pebble-quality-stock-{config,build,ctest}.log` (ephemeral).

The ARM command recorded above also passes for `soc/t5ai/touch_queue.c`, adding
`-Isubsys -Ibuild/src/fw` for the real syscall/font declaration dependencies,
with default configuration and `-DCONFIG_T5AI_IO_PROFILE=1`. The generated font
header is included but no font keys are used. Ruff, clang-format 23 and
`git diff --check` pass. No hardware was operated or qualified; a full
firmware/release link, on-watch latency and runtime SRAM headroom remain unverified.

**Next unresolved item: finding 6.** The SD adapter still intercepts the next
PFS allocation and exports `__wrap_ftl_*`. Reinspection confirms the allocation
is the page-flags cache inside `prv_build_page_flags_cache()`, not a generic PFS
heap requirement. The next increment needs an explicitly selected SD FTL backend
and a specific page-flags buffer boundary. Existing stock PFS unit tests disable
that cache under `UNITTEST`, so they must be supplemented by cache-enabled
production-unit coverage before replacing ownership behavior. Any shared
extension must preserve default heap allocation and be justified before editing.


### Operator-requested AP flash — 21 November, 23:36 UTC

The operator explicitly requested “flash first”, superseding the earlier
source-only/no-flash restriction for this installation. Flashed the current
working tree at HEAD `a253674e7`, including the uncommitted finding-5 transport;
no later source edit is represented by this image. Frozen source patch/files,
ELF/map/config/dictionary, raw/protected AP and logs are under
`build-quality-20261121/flash-20261121/`. Build ID:
`12a02047368b739cd94cfbbfb0d5b4fa90790909`. Phone version:
`v4.41.2-1650-ga253674e7-dirty`.

Built from a fresh directory with the same effective normal configuration as
`build-tuya-upstream`. The first attempt encountered missing Python build
packages; temporary environment installation resolved these tooling failures.
The optional zopfli optimizer initially changed PNG compression bytes. Removing
it from the temporary environment and regenerating candidate PNG outputs made
`system_resources.pbpack` byte-identical to the previous normal build; no
resource partition write was required. No production generator or guard changed.
The full firmware link, image map, CRC protection, current-version, resource-size
and syscall checks passed. Kernel heap is **32,800 bytes**, above the unchanged
28 KiB guard. The link emits generated app-API LTO type-mismatch warnings;
full logs are preserved and those shared-code warnings were not changed here.

Commands used (temporary environment contains the needed build packages):

```sh
PATH=/tmp/pebble-quality-venv/bin:$ARM_TOOLCHAIN/bin:/usr/bin:/bin \
  PYTHONPATH=tools/libs/pbl-cli CCACHE_DISABLE=1 \
  /tmp/pebble-quality-venv/bin/python -m pbl -b build-quality-20261121 \
  configure --board unhygienix \
  -DPython3_EXECUTABLE=/tmp/pebble-quality-venv/bin/python
# Same environment for: python -m pbl -b build-quality-20261121 build
/tmp/pebble-quality-venv/bin/python tools/flash_unhygienix_firmware.py \
  --raw build-quality-20261121/flash-20261121/pebbleos.bin \
  --protected build-quality-20261121/flash-20261121/pebbleos.t5ai-crc.bin \
  --tyutool /tmp/pebble-quality-tyutool \
  --port /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00 --flash
```

`ARM_TOOLCHAIN` above denotes the existing Arm GNU 14.2.rel1 installation at
`/home/baz/pebble/build/arm_gnu_toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi`.
The temporary tyutool wrapper invokes the existing 3.2.9 executable with
`-b 460800`. Actual write succeeded in **51.8 seconds**, AP-only physical range
`0x132000..0x2e0f5c` (end exclusive). Protected SHA-256:
`ca8df3bde36d05dd79ceb75b1eec8720d600dbcd9fbee02b1b6183fd97cafca2`.
The dedicated script verified CRC blocks, vectors, range and checkout metadata
before writing; the current-version guard was not bypassed. The exact previous
cleanup AP remains preserved at
`build-tuya-upstream/imu-motion-20261115/cleanup/` (protected SHA-256
`560c3d28d679b68a6baaa579fb99807d5de8be474085969a254e97854ff72193`).
CP, resource, PFS, identity/calibration and bond partitions were not programmed.

**Hardware failure preserved, not qualified:** the 120-second capture received
8,910 bytes, 5 AP boot banners and 4 early panics at
`0x02136a51`, around 0.183 seconds into those boots. Exact-ELF `addr2line`
resolves the panic to `prv_timer_count()` / `soc/t5ai/delay.c:47` (latch readiness
assertion). The eventual boot confirms the exact build ID, AP/CP reset sources
`0x71/0x10`, contract `0x78f`, existing SD PFS (`new=0`), valid system resources,
launcher startup and encrypted/bonded PPoGATT phone reconnection. This later
success does not erase the first failure. The capture is stopped; no additional
commanded reset, rollback or flash was issued. No on-watch gesture, timing,
latency, power or soak qualification is claimed.

**Next unresolved item is reopened finding 3**, before finding 6: determine why
the new bounded timer read rejects startup latches and extend production-unit
coverage for the real startup/clock behavior. The deadline/fallback model passes
host injection but did not predict these panics. Do not weaken or remove the
guard, increase its bound without evidence, or treat eventual boot as acceptance.
The finding-5 full-link limitation is now resolved; its hardware gesture and
runtime headroom qualification remains open.


### Finding 3 repair — 21 November, 23:47 UTC

The operator requested resolution of the timer-panic blocker. This is important:
unbounded register waits can hang the AP, but a faulty failure check can also
reset healthy hardware. The earlier guard implementation introduced the latter
regression; the original static review did not establish this new failure.

**Cause established from hardware:** a temporary failure-only assertion logged
latch state and the existing deadline/iteration budget, without changing either
bound. Its 140-second stopped capture contains 16 panics and 17 AP boot banners
before eventual startup. Each latch assertion retained **65,536 polls**
(`left=0x10000`) with age **0 or 1 AON tick**, and a clear latch in the diagnostic
snapshot. The short-circuit polling condition therefore observed completion
before decrementing its budget, but the new separate readiness read observed
busy and panicked. No timeout had expired. This establishes contradictory
successive status observations, not a deadline needing enlargement. The reason
for the hardware's transient busy value, including any cross-core participation,
is not established and is not assumed by the repair.

`soc/t5ai/delay.c` now retains the observation that exits the poll. The single
status load uses the real kernel MMIO API, enabling a modeled hardware endpoint
while retaining the complete production unit and real headers. There is no
shared/common source change. One millisecond / **32 AON ticks**, the **65,536**
poll fallback, stalled-counter fatal path, interrupt restoration, timer setup
writes and **26 MHz** scaling are unchanged. The temporary diagnostic message
was removed from the corrected production image. Linked ARM disassembly confirms
there is no additional status load on the success path.

The extended `tools/tests/test_t5ai_delay.py` reproduces ready/busy/clear under
both interrupt masks. A temporary complete-unit control restoring the second
status read fails at `completion re-read must not panic`; the fixed unit passes.
Existing live/stopped/wrapping AON, stuck latch, frozen counter, minimum-delay
and interrupt-restoration cases still pass. Commands/results:

```sh
ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest \
  tools.tests.test_t5ai_delay tools.tests.test_t5ai_uart \
  tools.tests.test_t5ai_uart_idle_clock tools.tests.test_t5ai_console -v
# 4 suites pass, ASan/UBSan.
PYTHONPATH=. /tmp/pebble-quality-venv/bin/python \
  /tmp/pebble-quality-timer-regression-proof.py
# Deliberately restored duplicate-read control fails; proof command succeeds.
/tmp/pebble-quality-venv/bin/ruff check --no-cache tools/tests/test_t5ai_delay.py
/tmp/pebble-quality-venv/bin/ruff format --check tools/tests/test_t5ai_delay.py
git diff --check
# All pass; changed C formatted with clang-format 23.
# Arm GNU 14.2 toolchain and temporary build environment:
PATH=/tmp/pebble-quality-venv/bin:/home/baz/pebble/build/arm_gnu_toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:/usr/bin:/bin \
  PYTHONPATH=tools/libs/pbl-cli CCACHE_DISABLE=1 \
  /tmp/pebble-quality-venv/bin/python -m pbl -b build-quality-20261121 build
# Full ARM/LTO link, log-hash, map, CRC, resource-size and syscall checks pass.
```

The diagnostic build initially exceeded the log format's seven-argument limit;
the normal guard rejected it before flashing. Reducing the temporary message
to seven arguments allowed the diagnostic build. Ruff initially could not write
the sandboxed cache; `--no-cache` resolves this environment limitation. Neither
failure justified a guard/source-policy change. The existing generated API LTO
warnings remain logged and outside this SoC-local increment.

**Corrected AP installed:** build ID `a967c9ad05ee7835cc8b8fe9d1ec7934a0cc97d4`, version
`v4.41.2-1650-ga253674e7-dirty`, protected SHA-256
`ec7831959ebce98ad4aec9369aa5b75ca345425c2e39169e05069c1205112b23`.
Tyutool 3.2.9 at 460800 baud wrote AP-only range `0x132000..0x2e0f5c` in
51.8 seconds, using the dedicated validated script with no version bypass.
Normal configuration and system resource pack exactly match the previous
installation; kernel heap remains **32,800 bytes**. CP/resources/PFS/calibration/
identity/bond partitions were not programmed. One diagnostic AP flash and one
corrected AP flash were performed; no separate reset, rollback or provisioning
operation was issued.

The final stopped capture received **3,788 bytes over
150.3 seconds**, with **one AP boot and zero panics**. Exact
build ID, launcher startup, existing SD PFS (`new=0`) and valid system resources
are observed. Bonded encrypted phone session observed: **yes**.
The 150-second capture spans flashing and approximately 94 seconds after the
first boot bytes. The known post-flash reset sources `0x4f/0` and
**sleep-admission reason 4** remain a separate release blocker. They were preserved; no reset was issued to
clear them. This bounded first-boot result resolves the reproduced timer false
panic, not waveform, gesture, retained-return, power or soak qualification.

Exact diagnostic and corrected evidence are preserved separately under
`build-quality-20261121/{timer-diagnostic-1,timer-fix-1}/`, including ELF,
source snapshot/diff, configuration, raw/protected images, dictionary, hashes,
build/test/flash logs, raw UART/chunk timestamps, decoded trace and result.
The first failed installation at `flash-20261121/` remains intact.
The installed working tree also contains the pending finding-5 touch increment;
those production changes are separate from this timer repair.

**Next backlog item after the pending finding-5 increment: finding 6**, the SD/PFS allocation/provider
and FTL selection boundary. The pre-existing sleep-admission/release blockers
remain open in the hardware/power work list and are not cleared by item 3.


### Increment 6 — explicit SD PFS backend and cache provider, 22 November

**Implementation and shared-change justification:** PFS now requests/releases
only its page-flags cache through `page_flags_cache.h`. Default inline providers
retain the exact kernel heap calls; only Unhygienix SD selects the private
8,192-byte PSRAM provider. The next-allocation flag and source-wide
`pbl_malloc_check`/`pbl_free` redirection are removed. Ordinary PFS file/name/page
allocations retain the kernel heap. Rebuild/zero-size/reacquisition, allocation
size, exclusive ownership and duplicate-release assertions are covered.

Two hidden generic Kconfig capabilities select a platform FTL and a specific
cache provider. The filesystem CMake file keeps the stock NOR implementation
unless explicitly selected otherwise; Unhygienix SD selects both capabilities.
The SD adapter exports the ordinary `ftl_*` signatures. Its FTL linker wrappers
and the NOR unit's special non-LTO compilation are removed. The narrow
`__wrap_pfs_init` and non-LTO PFS unit remain deliberately: they preserve header
validation before the real initialization and header publication only after
successful completion. Removing that separate lifecycle boundary belongs to the
later integration cleanup and would need its own pre/post-init API.

The small shared extension is unavoidable without retaining allocation-order
interception or wrapping a global flash API: the stock PFS cache pointer is
private and there was no board-local buffer boundary. No board names are added
to shared PFS policy. Default heap allocation and the NOR backend remain intact,
with cache-enabled complete-unit tests as well as the stock filesystem suites.
No public app interface, common timing policy, AP/CP ABI, partition geometry,
PSRAM/retained reservation, calibration or Bluetooth identity changes.

**Newly reproduced dependency:** the older SD fixture extracted only the cache
builder and replaced `pfs_format()` with a model calling
`__wrap_filesystem_regions_erase_all`. Production PFS no longer calls that hook:
it directly erased `PART_FILESYSTEM` on NOR. Compiling the complete production
PFS and SD units from HEAD `8276651e6` with their original allocator/linker
boundaries aborts specifically at **“SD PFS format reached physical NOR”**.
This is a host reproduction of the real call path, not evidence that hardware
was formatted or NOR contents were inspected.

Whole-volume erase now has the narrow internal `ftl_erase_all()` boundary.
The default NOR implementation contains the identical former partition-erase
statement; SD erases only its existing 32 MiB card region. `pfs_format()` still
holds its normal lock and writes the same page headers. Existing 512-byte volume
header layout, sector 2048/header+128 data offset, version/inverse checks,
require-existing provisioning guard, bitwise NOR-write semantics, locks,
workspace ownership and fail-stop-on-I/O-error policy are preserved. The dead
obsolete erase wrapper is removed. No format/provision operation was performed
on hardware.

**Validation:**

```sh
ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest \
  tools.tests.test_t5ai_sd_pfs_integration tools.tests.test_t5ai_sd_pfs \
  tools.tests.test_t5ai_sd_idle_clock tools.tests.test_t5ai_sd_irq_race \
  tools.tests.test_t5ai_config -v
# 19 test methods pass.
PYTHONPATH=. /tmp/pebble-quality-venv/bin/python \
  build-quality-20261121/pfs-increment-6/baseline-proof.py
# Pinned old full-unit control reproduces the wrong-backend format failure.
PATH=/tmp/pebble-quality-venv/bin:/usr/bin:/bin CCACHE_DISABLE=1 \
  cmake --build /tmp/pebble-quality-stock-tests \
  --target test_pfs_emery test_fs_emery -j 4
/tmp/pebble-quality-venv/bin/ctest --test-dir /tmp/pebble-quality-stock-tests \
  --output-on-failure -R '^(test_pfs_emery|test_fs_emery)$'
# Both stock filesystem suites pass; the PFS suite contains 32 cases.
```

Six new integration methods compile complete PFS and CRC production units with
real board/configuration, FTL, cache, kernel and SD headers. Cache is enabled
(`UNITTEST` is not defined). Only host storage, heap and scheduling endpoints
are modeled; no PFS operation is substituted. They cover SD provision/recognized
header paths, all five header-field refusals before writes, real file create/
close/reopen/read, zero-size and repeated cache rebuilds, full-volume exhaustion,
delete/GC/reuse, actual format through the last subsector, outside-volume
sentinels, partial/multi-sector operations, one-way writes, cache hits, capacity/
I/O/bounds errors and cache ownership assertions. The NOR variant compiles the
complete stock FTL with default heap-backed cache and verifies rebuild, reopen,
format and physical partition isolation. Normal integration uses ASan/UBSan;
the LTO variant uses UBSan, matching the previously recorded local LLVM gold/
ASan-constructor tooling limitation. Existing SD driver tests retain their
modeled API declarations; they are not a hardware timing/DMA qualification.

The old extracted builder/model-PFS fixture is removed from
`test_t5ai_sd_pfs.py`; its one-/four-bit DMA driver cases remain, with the new
complete integration coverage in `test_t5ai_sd_pfs_integration.py` and readable
`fixtures/t5ai_sd_pfs/` sources. Initial new-fixture compile failures were
incorrect/missing endpoint declarations, corrected against real headers.
The first stock build command used an unsuffixed target; this test matrix uses
`test_pfs_emery` / `test_fs_emery`, which built and passed. These were harness/
tooling issues, not failures bypassed by changing production guards. Formatting
uses clang-format 23 on changed C hunks; Ruff check/format and `git diff --check`
pass.

The exact final normal ARM/LTO firmware build also passes, using the same
Arm GNU 14.2 and temporary build environment recorded above:

```sh
PATH=/tmp/pebble-quality-venv/bin:/home/baz/pebble/build/arm_gnu_toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:/usr/bin:/bin \
  PYTHONPATH=tools/libs/pbl-cli CCACHE_DISABLE=1 \
  /tmp/pebble-quality-venv/bin/python -m pbl -b build-quality-20261121 build
```

Log hash, map/layout, protected CRC/vector/current-version, resource-size and
syscall checks pass. Ninja's actual compile database excludes the stock NOR FTL
for this SD image; the ELF exports direct SD FTL/cache-provider symbols and has
no `__wrap_ftl_*` or allocator-interception symbols. Kernel heap remains
**32,800 bytes**, above the unchanged 28 KiB guard. The system resource pack is
byte-identical to the previous installed pack; the private flags range remains
exactly 8,192 bytes at its pinned address. Build ID:
`8d287c9204ad44a2dbc96219b851b6810bef9171` (dirty working tree at `8276651e6`,
including the pending finding-5 touch increment). Artifacts, source snapshot,
hashes, baseline proof, build/test logs and selection checks are preserved under
`build-quality-20261121/pfs-increment-6/`. Existing generated app-API LTO warnings
remain outside this SoC/storage increment. No firmware from this increment was
flashed; installed-state records still describe the timer fix image.

The pending touch increment was rechecked before starting this work:
`ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest
 tools.tests.test_t5ai_touch_queue tools.tests.test_t5ai_touch -v` — **17 pass**.
Its earlier bounded first-boot evidence and gesture/latency qualification limits
remain as recorded. Finding 6 is now implemented and verified at host/build
level. No hardware was flashed, reset, provisioned or formatted in this increment;
there is no hardware qualification claim.

**Next unresolved item:** the newly listed post-flash sleep-admission release
blocker, before broader findings 7–8. Source inspection confirms reason 4 is
`T5AISleepFault_ResumeTimeout`: startup rejects either the saved AP sleep-timeout
reason or an unfinished retained breadcrumb. The watchdog guard deliberately
sets `T5AI_RESET_REASON_SLEEP_RESUME` (`0x4f`) while armed and restores its old
reason only after the retained return. A programming reset can therefore
intersect a legitimate guard interval. The existing traces do not prove whether
that is the observed cause, whether an actual guard expired, or which quiescence/
reset-intent boundary should change. Do not suppress `0x4f`, clear valid evidence,
relax ownership checks or enable CP deep sleep as a workaround. Preserve first-
boot traces and investigate the lifecycle distinction before choosing a repair.


### Historical alternative — 22 November, AP-only programming handoff (superseded)

Reproduced the failure using **identical** current-HEAD source and AP artifacts
(`110701cc8`, build ID `2c37f3329018607139b9a9bc7f0a8e7a25941c56`). The first
AP-only installation booted with AP/CP reasons `0/0`, contract `0x78f`, existing
32 MiB SD (`new=0`), valid resources, bonded/encrypted PPoGATT reconnection and
32 AP-only returns with zero CP/clock/mask/resource transitions and zero faults.
Reflashing the very same bytes from that healthy retained-idle runtime produced
first-boot AP/CP reasons `0x4f/0`, reason 4 at generation 0, CP previous stage /
generation `0/0`, and boot PMU reset word `0x4f000003`. Both writes took 51.8 s;
no intervening manual recovery reset was used. This establishes a programming
lifecycle dependency, rather than a version-dependent admission regression.
The original watchdog guard tags an armed interval with `0x4f`; an external
programming reset does not execute the AP's guard-restore path. The firmware
cannot safely infer programming intent from the same timeout marker used for
a genuinely interrupted retained return.

Repair scope: the existing pinned CP `ap_cmd` bridge is present in the received
CP shell help. Add an AP/SoC-local receiver for its existing log-channel input,
without changing CP firmware, shared Pebble C, pinned message numbers or native
message layout. A fresh 16-hex-digit nonce is copied into AP-owned storage before
the CP command-buffer ACK. Deferred, bounded work asks the retained coordinator
to stop new entries, finish existing return/cancel/cleanup and validate its awake
contract. Only a clean idle coordinator emits `T5FLASH READY <nonce>`; faults,
unfinished evidence, timeout and uncertain cleanup never do. The programming
hold lasts until reset; there is no automatic clearing of fault evidence or
automatic recovery reset. New AP flashing requires this reply; pbl's runner
requires one before each reset-producing write. CRC/range/version and provenance
guards still precede any device command. Dry-run / validation remain read-only.

The new complete production receiver and real kernel tick unit run with real
kernel, mailbox, timeout and sleep headers under ASan/UBSan. Modeled endpoints
cover the CP-owned input lease and buffer reuse, strict command length/range /
nonce validation, duplicates, mismatched requests, deferred polling, failure and
timeout. Coordinator scenarios cover queued preparations, armed cancellation,
pending resume events, failed cleanup, unfinished traces, live timeout markers
and sticky owner faults; the existing coordinator harness still models SRAM WFI
and hardware endpoints. Host tests require a complete fresh nonce-matched line,
reject echo/stale/partial replies, and verify refusal/missing console prevent the
runner from invoking tyutool. Restoring pre-fix admission behavior in memory is
rejected by the new regression; production source was not altered for that proof.

Validation so far:

```sh
ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest \
  tools.tests.test_t5ai_programming tools.tests.test_unhygienix_runner \
  tools.tests.test_t5ai_retained_sleep tools.tests.test_t5ai_reset \
  tools.tests.test_t5ai_sleep_guard -v
# 40 tests passed; /tmp/pebble-sleep-validation.log
ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest \
  tools.tests.test_t5ai_config tools.tests.test_unhygienix_boards \
  tools.tests.test_unhygienix_image tools.tests.test_unhygienix_image_packaging -v
# 39 tests passed; /tmp/pebble-sleep-image-tests.log
# Normal ARM/LTO: same environment and pbl build command recorded above;
# /tmp/pebble-sleep-final-build.log, successful CRC/map/resources/syscall guards.
```

Candidate build ID `1855111f5362c1deef582b8372760f286b4440b9`, protected SHA-256
`b88f37b524e6897c9220cb513876d93596a518dd7eedb687e5034e158f52021b`.
Kernel heap 32,768 bytes remains above the 28,672-byte guard. Retained sections
remain at exactly `0x280103a0` (136 bytes) / `0x28010440` (16 bytes); the 8,192-byte
PFS flag buffer remains at `0x60020000`. System resources are byte-identical.
No watchdog period, clock/lead/deadline, retained structure, AP/CP numeric ABI,
partition, calibration, Bluetooth identity or CP default was changed.

The one-time bootstrap used the preserved HEAD flasher against the old AP's
already-quarantined, awake runtime; that bootstrap is not handoff qualification
and there is no production bypass option. Candidate installed by AP-only write
in 52.0 s and booted with reasons `0/0` / contract `0x78f`. A direct no-flash
handoff then returned a fresh READY in 139 ms. The subsequent normal AP flash
entry point received a new READY in 140 ms before programming and first-booted
with reasons `0/0` / contract `0x78f`, without a post-flash recovery reset.
Both normal handoff-controlled flashes now passed their **first** boot: one AP
banner, zero panics, exact installed build ID, reasons `0/0`, contract `0x78f`,
32 clean AP-only returns, zero CP/clock/mask/resource transitions and fault zero.
Both reopened the existing SD volume (`new=0`), found valid resources, launched
the launcher and restored bonded/encrypted PPoGATT. The second trial began from
the first trial's verified retained-idle runtime, reproducing the old failure's
precondition. Its fresh READY took **140 ms**, followed by a **52.1 s** AP-only
write and the successful first boot at **09:07:09 UTC**. There was no post-flash
manual/automatic recovery reset, rollback, CP rewrite or provisioning. The
first controlled write took 52.0 s; its first boot was 09:04:11 UTC.

The stopped successful captures received 3,782 / 3,783 bytes over 160.4 / 160.2 s
respectively (including programming and roughly 108 s after first boot bytes).
The additional dedicated-flasher entry-point regression passed along with the
other four programming tests (`/tmp/pebble-sleep-final-entry-tests.log`), proving
that missing/same console and a refused handoff cannot invoke tyutool in either
supported AP entry point. Total distinct targeted methods: 41 plus 39 image /
configuration methods. Ruff and changed-hunk clang-format checks pass.

Evidence roots under `build-quality-20261121/`: `sleep-baseline-20261122/`,
`sleep-reflash-20261122/` (failure), `sleep-candidate-20261122/` (bootstrap), and
`sleep-handoff-1-20261122/` and `sleep-handoff-2-20261122/`. Each preserves exact images/map/config/dictionary /
hashes, first-boot UART bytes/chunk times and flash evidence. The handoff JSON
preserves literal bytes and nonce result. No CP/resources/storage/identity/bond
partition was programmed or formatted. CP replacement, abandoned-hold recovery,
electrical timing, repeated fault injection and long-duration qualification are
not established by these bounded tests. Work remains uncommitted for operator
verification, as requested; broader backlog work stays paused after this issue.


**Historical handoff scope (superseded by the boot-retry implementation below):** the AP-only post-programming admission issue was repaired
and verified in two bounded first-boot trials. The independent same-image
failure remains preserved. The watchdog timeout namespace, marker and startup
quarantine decision are unchanged, so unexpected interrupted-return evidence
continues to refuse admission; the new workflow avoids creating that evidence
by waiting for a clean AP handoff before the external reset. No general retained-
sleep, storage, electrical or release/soak qualification is implied.

**Paused for operator verification before commit.** Leave build
`1855111f5362c1deef582b8372760f286b4440b9` installed; captures are stopped and the
final boot is running normal retained idle (no programming hold was requested
after that boot). Changes are uncommitted. Unrelated planning edits in
`boards/unhygienix/tuya/REMAINING_TASKS.md` and `power-todo.md` remain untouched.
After review/commit, the next ordered backlog item is finding 7; no work on it
was started in this increment.


### 22 November: investigate evidence-preserving boot retry

The operator permits reboot on a genuine failed sleep attempt and requested
investigation of retaining evidence without disabling subsequent sleep. This
reassesses the cross-boot quarantine requirement above; the programming-handoff
implementation remains uncommitted and is not the selected final solution.

The current startup branch treats saved AP reasons `0x40..0x4f` or a valid
unfinished AP trace as a new runtime fault, before validating the new boot.
This is inherited local policy from earlier coordinated CP-sleep diagnostics.
It is separate from current-boot ownership/cleanup quarantine. The AP native
alarm initializer already queries the surviving CP generation and requires
Cancel and FinishCleanup responses for that owner before initializing. The CP
rejects unsafe cleanup (including active resources or analog ownership), and
the coordinator separately checks the awake contract. No ABI or retained-layout
change is needed to exercise this existing path after a previous interrupted
attempt. Existing SRAM restore, missing-controller-event and watchdog paths
already reset the whole chip on a hung return.

A temporary experiment in `/tmp/investigate_t5ai_sleep_retry.py` changes only
an in-memory copy of the startup policy: log previous interruption, run the
existing diagnostic/contract initialization, and allow a fresh AP-only attempt
when validated. Production source was not changed by the experiment. Command:

```
PYTHONPATH=. ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python /tmp/investigate_t5ai_sleep_retry.py
```

Result: 48 cases passed under ASan/UBSan (16 saved reasons, each with healthy
initialization, failed initialization, and deferred peripheral votes). The
previous AP stage/generation and saved reason survive startup; successful new
attempts complete cleanup; failed initialization remains faulted and cannot
enter. This reuses the existing modeled coordinator harness, including modeled
WFI/MMIO and alarm endpoints; it does not qualify hardware restoration or prove
surviving CP cleanup end-to-end. A harness-only zero-startup-delay compiler
warning was corrected by retaining the production 30-second delay in the
experiment and advancing modeled time; production timing was unchanged.

Remaining: implement a minimal AP-only startup policy with a snapshot/report of
previous evidence before the next transaction overwrites its retained slot;
keep experimental coordinated-CP modes conservative. Verify actual native
alarm initialization failures and cleanup refusal, then build and capture the
first boot after a raw AP-only programming reset without the handoff. Distinguish
ordinary admission vetoes (busy owners or inadequate idle window) from fatal
sleep/restore failures: normal vetoes should not cause reboot. Current runtime
sticky faults still disable retained sleep, so a separate explicit policy change
would be needed if those should reboot instead. Repeated genuine failures may
reboot repeatedly under the requested retry policy; no boot-counter framework
has been introduced. Preserve first-failure host captures. This investigation
is not implementation completion or hardware qualification; no new checkbox is
checked and no flash, reset or commit was performed.

Existing production alarm/reset/watchdog regression validation also passed:
`ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest
tools.tests.test_t5ai_native_alarm tools.tests.test_t5ai_reset
tools.tests.test_t5ai_sleep_guard` — 26 test methods in 42.261 seconds,
including 98 modeled CP alarm cases and 39 AP alarm cases. Evidence:
`/tmp/investigate_t5ai_sleep_existing.log` and
`/tmp/investigate_t5ai_sleep_retry.log`. `git diff --check` passed.


### 22 November: implement boot retry; preserve first missing-return checkpoint

Replaced the uncommitted programming handshake with a T5AI-only startup policy.
The previous AP trace is copied using its production SleepTrace type into a
16-byte volatile ordinary-RAM snapshot before another attempt can replace the
retained slot. Production CP-awake AP-only mode reports previous interruption
and runs existing owner cleanup/awake-contract validation. Experimental CP-sleep
modes and all current-boot guards remain conservative. Removed this task's
uncommitted programming receiver/API, host helper, flasher/runner requirements
and associated tests; their prior source is backed up under
`/tmp/sleep-handoff-before-replacement/` and existing hardware evidence remains.
Unrelated pre-existing changes in REMAINING_TASKS.md and power-todo.md are untouched.

Host validation: 44 methods in retained-sleep/native-alarm/reset/watchdog modules
passed under ASan/UBSan (145.383 s), including 23 new boot-policy scenarios and
six failed-initialization/cleanup/ABI cases against the full native AP alarm
unit. Existing diagnostic quarantine tests remain unchanged. Ten existing
runner/flasher tests passed (17.548 s). The final volatile-snapshot change was
rechecked with the 23-case boot-policy method (2.445 s). Ruff without cache,
formatting and diff checks passed. A sandbox-only Ruff cache write failure was
resolved with --no-cache; it was not a source failure. Normal ARM/LTO builds,
log hash, map/layout, CRC/vector/current-version, resources and syscall guards
passed. Build logs: `/tmp/sleep-retry-build.log` and
`/tmp/sleep-retry-final-build.log`.

Frozen first candidate: build ID `a6c8e503689790c77b26e3450c42a8ceedfc4544`,
protected SHA-256 `0e101e6bb5c71b76146a237ab8ca31d61f3312f2b257db32445a3a2992a24e46`,
AP physical range `0x132000..0x2e0f18` (exclusive). Retained data is still
136 bytes at 0x280103a0, AP trace 16 bytes at 0x28010440 and private PFS flags
8192 bytes at 0x60020000. Resources match the previous installed pack.
The host lab file has no configured watches; actual host enumeration and a
CP help response (752 bytes) verified the dual UART before capture.

Raw AP-only flash without handoff completed in 51.7 s. Its FIRST boot at
09:43:50 UTC retained AP/CP reasons `0x4f/0`, reported the interruption and
boot PMU `0x4f000003`, validated contract `0x78f`, opened existing SD PFS
(new=0), found valid resources, launched and restored bonded/encrypted PPoGATT.
There was one boot and no sleep-failure/panic record. However, the expected
32-return report did NOT appear in the 180.3-second capture (4502 bytes,
including programming). This is an incomplete sleep validation, not success.
A same-boot continuation is preserved separately; no recovery reset was used
to hide the missing checkpoint. Evidence: `build-quality-20261121/sleep-retry-1-20261122/`.

A temporary one-shot SysWorkq diagnostic is being used to read coordinator
phase, owner/fault state, physical/logical/HCI readiness, deadline budget and
attempt/return counters after 60 seconds. It changes no admission decisions and
will be removed before final validation. No checkbox is checked for this new
policy until retained returns are actually observed. No CP, resource, storage,
calibration, identity or bond partition has been programmed or formatted.


### 22 November: evidence-preserving boot retry implemented and verified

**Final production scope:** only `soc/t5ai/sleep.c` changes. A saved interrupted
attempt now prompts a warning and the existing startup diagnostic/owner-cleanup/
awake-contract checks in production CP-awake AP-only mode. A volatile 16-byte
ordinary-RAM copy retains the previous AP trace throughout subsequent attempts.
Saved boot reasons remain intact. Existing watchdog/SRAM/missing-controller
reset guards, runtime ownership/cleanup quarantine and experimental CP-sleep
quarantine remain unchanged. No new boot-counter/recovery framework, programming
mode, CP command/API, partition or retained layout was added. A hung return may
still reboot through the existing guards, as accepted by the operator.

Also route a latched post-wake fault through `prv_fail()` after successful
cleanup. Previously this path silently assigned SleepFailed; it now logs the
real fault while preserving the same closed admission and owner-fault state.
A new normal-mode late-wake regression checks exact fault reporting, completed
cleanup and refusal of a subsequent entry. No deadline threshold was relaxed.
This fixes an observability gap found during validation; an actual late wake
was not established as the cause of the earlier missing 32-return report.

The temporary one-shot diagnostic image (build
`df5da8922fab4c3ae5345a45e0bbb5d77279f86d`) first-booted with `0x4f/0` and at
09:52:43 UTC reported phase=Idle, initialized=1, owners=0, faulted=0,
physical/logical/HCI readiness=7, 23 requests, 20 returns, 3 aborts, fault=0,
rejection=0 and startup settling complete. It established real sleep progress
rather than merely a successful boot. Its 170-second capture still did not
receive the 32-return report, so that run is not counted as passing the full
checkpoint. The earlier missing-report runs remain inconclusive as to cause.
Probe code was removed before the final build and installation. Its source,
image and raw evidence are preserved under `sleep-retry-probe-20261122/`.

Final normal ARM/LTO build passed all generated image guards; log:
`/tmp/sleep-retry-fault-log-build.log`. The full final retained-sleep,
native-alarm, reset and watchdog suites passed **45 methods** in 60.167 seconds
under ASan/UBSan (`/tmp/sleep-retry-final-validation.log`). This includes all
unchanged diagnostic-quarantine cases, new production boot-retry cases,
failed warm-owner cleanup/ABI cases and late-wake reporting. The restored
flasher/runner's **10 methods** also passed. Ruff check/format, clang-format
and `git diff --check` pass. Host endpoint/WFI models remain host evidence;
no tests were changed to ignore an unsafe result.

**Installed final image:** build ID `31b50d165281fa78016a880573b910fefe8c69cf`,
version `v4.41.2-1653-g110701cc8-dirty`, raw SHA-256
`c97738d3597d22f0d27ff9af423695cdf3a5e092a6ca01fc904d355c1f80ddba`,
protected SHA-256 `55c07280616340f63aed9e599594e3fb1a1a89da92ca639aa6bf4cdbfe647e72`.
The raw AP-only flash took 51.8 seconds, physical range
`0x132000..0x2e0f18` exclusive. No programming hold/handshake or extra recovery
reset was used. Its FIRST boot at **09:57:43 UTC** retained AP/CP reset sources
`0x4f/0` and boot PMU reset `0x4f000003`, reported the previous interruption,
and validated contract `0x78f`. At **09:58:18.607 UTC** it reported **32 AP-only
returns, zero CP/clock/mask/resource transitions and zero faults**. The stopped
capture has 4161 bytes over 158.298 seconds including programming, exactly one
boot, the expected build ID and no panic/sleep-failure record. Existing SD PFS
(new=0), valid resources, launcher and bonded/encrypted PPoGATT recovery passed.
This is the final image's first boot, not a later successful reset.

The kernel heap is 32800 bytes with the unchanged 28 KiB guard. Pinned retained
and PFS flag sections match the addresses/sizes recorded above; resources remain
byte-identical. No CP/resources/PFS/calibration/identity/bond partition was
programmed or formatted, no shared/common Pebble source changed, and ABI,
active clock policy and timing constants are unchanged. No fault-injection,
electrical, power or soak qualification is claimed. Capture processes are
stopped; the device is left running the final production image in normal idle.

Reviewable evidence and exact production/test source snapshots:
`build-quality-20261121/sleep-retry-final2-20261122/`. Installed source identity
was checked against the frozen candidate. The removed handoff alternative and
all first-missing-checkpoint evidence remain separately preserved. This follow-up
is complete at the recorded host/build/bounded-hardware scope. The operator
subsequently authorized committing this verified increment. **Further backlog
work remains paused.** The next unresolved backlog section is finding 7.
