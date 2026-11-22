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

- [x] Separate diagnostic instrumentation from production integration in
  `soc/t5ai/CMakeLists.txt`; inventory renames, source suppression, wrappers and
  source-specific compilation changes.
- [x] Prefer explicit backend selection and a few documented extension points.
  Remove empty CPU-metrics deferred-function scaffolding.
- [x] Acceptance: release/LTO links select exactly the intended implementations;
  retained interception has a documented purpose and link-level verification.

### 8. Separate qualified sleep from experimental modes

- [x] Isolate the qualified CP-awake production path in `soc/t5ai/sleep.c`,
  `boards/unhygienix/tuya/cp/native_alarm.c` and `sleep_protocol.h` from the
  accumulated CP transition investigations.
- [x] Replace interdependent experimental mode booleans with named policies and
  explicit transitions. Preserve numeric ABI values; reject unsupported modes.
- [x] Acceptance: cancellation, late replies, cleanup and production state
  transitions pass; no CP low-voltage behavior is enabled by cleanup.

### 9. Make HCI wakeups event-driven

- [x] Map every producer of HCI work, then use existing notifications as the
  normal wake mechanism in all configurations.
- [x] Replace periodic 5/100 ms polling in the event-wait policy with
  waits timed to actual retry, receive-retention and recovery deadlines.
- [x] Acceptance: lost-wakeup/race tests pass; operator accepts finding 9 as
  qualified on 2026-11-22, assuming auto-reconnect failures are phone-side for
  now. The incomplete matched connected-idle/latency comparison is accepted as
  a measurement limitation, not recorded as a passing test.

The event/deadline implementation is qualified by explicit operator acceptance
and is included in the operator-requested finding-9 commit. Event waits are
now enabled by default; an explicit `CONFIG_T5AI_HCI_EVENT_WAIT=n` retains the
polling fallback. Phone-side causation remains an accepted working assumption,
not a proven root cause. The connected candidate A/B measurement remains
incomplete. Original failures and measurement limits remain recorded below. See the
[producer/wait inventory](hci-work.md).

### 10. Give graphics optimizations explicit boundaries

- [ ] Benchmark `soc/t5ai/compositor_fast.c`, `pixel_fast.c` and
  `text_node_cache.c` independently; retain measured benefits.
- [x] Replace hidden stock-function renaming with explicit eligibility/entry
  points where feasible. Derive geometry from bitmap/board configuration and
  assess the compositor's row-extent scanning cost.
- [ ] Acceptance: differential stock comparisons cover clipping, blending,
  circular rows, unusual layouts and text-cache freshness; performance is measured.

### 11. Make PSRAM pool placement robust to upstream renames

- [ ] Replace `soc/t5ai/framebuffer.ld`'s capture of NimBLE private `.bss`
  symbol names with explicit storage sections or supported pool backing storage.
- [x] Until then, make map validation assert expected objects, sizes and ranges.
- [x] Acceptance for current placement: upstream symbol/size drift fails visibly
  instead of silently moving pool capacity into scarce internal SRAM.

The private-name replacement remains open. The guarded current-placement
increment below does not claim a supported storage-hook migration.

### 12. Move singleton driver configuration into native devices

- [x] CO5300: obtain the actual device instance from the class argument and
  store mutable state and serialization in that instance.
- [x] CST92xx: migrate its mutable state, work, retry and IRQ ownership into
  the actual touch instance.
- [x] CO5300: consume the supplied instance's board-defined SPI/reset wiring;
  preserve existing board timing configuration.
- [x] CST92xx: move pins, bus references, orientation and dimensions into board
  device configuration instead of globals and hardcoded driver constants.
- [x] Acceptance: existing board behavior and initialization order are preserved;
  both drivers can be exercised with separately configured test instances.

Panel and touch source increments below pass host isolation and sequence tests.
Targeted on-watch touch checks now pass with operator confirmation; finding 12 is complete
within the recorded bench scope. Warning/soak limits are retained below.

### 13. Use scheduler sleeps for long panel delays

- [x] Establish calling-context requirements for CO5300 reset/sleep sequences,
  then replace 120–200 ms busy waits with native thread sleeps in scheduler context.
- [x] Keep short timing delays and any necessary early-boot fallback.
- [ ] Acceptance: minimum timings and command/error sequences remain correct;
  startup/recovery responsiveness is measured.

### 14. Schedule battery work from events and deadlines

- [x] Replace the permanent one-second servicing poll in
  `drivers/battery/t5ai.c` with charge-edge and ADC-completion work, plus delayed
  debounce/retry deadlines.
- [x] Retain slower missed-edge reconciliation if required, asynchronous
  measurement semantics and invalid-reading protection.
- [ ] Acceptance: charge bounce, missed edges, ADC errors/retries and startup
  behavior pass; idle wakeups decrease without delayed state delivery.

### 15. Separate flash and ADC initialization ownership

- [x] Remove SARADC initialization ownership from `t5ai_flash_ipc_init()`.
  Keep shared MB-IPC initialization in the transport and make flash/SARADC
  explicit, idempotent clients with any required socket order documented.
- [x] Acceptance: either client's failure/retry is independently covered and
  storage startup does not depend unnecessarily on ADC setup (production-unit
  host fault tests; no new on-watch qualification).

### 16. Track logging recursion by actual thread identity

- [x] Review unregistered native logging contexts: all currently share the
  `PebbleTask_Unknown` flag in `pebble/kernel/kernel_applib_state.c`.
- [x] Route ordinary port diagnostics through registered work queues where
  appropriate; use actual thread identity if independent native logging is needed.
- [x] Acceptance: two distinct unknown threads do not appear to be recursive
  logging, while same-thread recursion and mailbox-owner exclusion stay safe
  (complete-unit host concurrency/fault tests; no on-watch qualification).

### 17. Replace shared SoC policy checks with precise capabilities

- [x] Use a precise recovery-boot capability for factory-reset recovery operations
  instead of `SOC_T5AI`, after checking the exact semantics.
- [x] Move Unhygienix pairing button assignments into board configuration with
  stock defaults, rather than naming the board in shared pairing UI code.
- [x] Acceptance: other boards retain their reset and pairing behavior; the
  Unhygienix unsupported-install guards remain effective (host capability/order
  matrices and default ARM build; no hardware reset/pairing qualification).

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


### 22 November: finding 7 — explicit production build integration

Reassessed at `e5af7e6e1` on branch `tuya`, preserving the two pre-existing
unrelated edits in REMAINING_TASKS.md and power-todo.md. The 398-line SoC CMake
file still mixed production integration and diagnostic instrumentation, used
HEADER_FILE_ONLY to suppress two stock sources, and scheduled an empty
CPU-metrics deferred callback. The SD FTL already had explicit backend selection
from finding 6; that boundary was retained rather than reimplemented.

Production rules now live in `soc/t5ai/cmake/production.cmake`; optional
instrumentation lives in `diagnostics.cmake`. The top-level file retains native
SoC sources, fixed memory scripts, assembly roots and size policy. All 11
surviving diagnostic deferred-function bodies compare byte-for-byte with HEAD;
Kconfig gates remain unchanged. The empty CPU-metrics callback was removed.
The real ARM diagnostic compile then exposed an existing unused `ticks` read in
`task_cpu_metrics.c`; removed that dead read and kept -Werror enabled. CPU metrics
and reminder profiler objects now compile together against production headers.
This is object/configuration coverage, not a fully linked diagnostic matrix.

The SoC-local backend selector explicitly removes exactly one stock source from
its owning target and requires exactly one selected local implementation. It
replaces global source suppression for process regions and flash-log storage,
without editing shared code or the stock target's remaining sources/properties.
Upstream target/source drift, duplicate stock sources and a missing local
backend fail configuration. Disabled flash logging is covered separately.
The existing unsupported flash-log-storage policy and STAR-MC1 non-shareable
app/worker mapping are preserved; no storage contents are changed.

A release configure also exposed an inherited unconditional console boundary:
CONFIG_RELEASE=y disables CONSOLE, so its deferred set_property addressed a
non-existent directory. Console-only wrappers and the no-LTO boundary now follow
CONFIG_CONSOLE. Normal console behavior and mailbox-owner exclusions are intact;
the actual console-disabled release build succeeds. This remains a board-local
manual release configuration; no release-matrix inclusion was added.

The existing image-validation gate now checks the final ELF using toolchain nm.
Its wrapper manifest comes from the ELF target's actual link options, including
common allocator and enabled diagnostic wrappers. Undeclared resolved wrappers
or missing selected production console/touch/PFS boundaries prevent packaging;
no symbols are artificially retained to satisfy the check. Backend ownership is
checked at configure time. Graphics/log-state renames and remaining touch/PFS/
console interception are inventoried with their purpose and LTO requirements in
[build integration](build-integration.md). Broader graphics API replacement and
benchmarking remain finding 10, not an excuse to change shared code here.

Validation:

- All **1194 normal compilation commands are exactly unchanged**, with no
  added/removed translation units or source-specific flag/rename changes. All
  **12 normal wrapper declarations are unchanged**. Diagnostic deferred bodies
  also compare unchanged, apart from removal of the empty callback.
- **15 focused host test methods pass**: real CMake backend selection/drift,
  missing/undeclared ELF boundary rejection, existing complete-unit touch/PFS
  LTO behavior and diagnostic configuration compatibility. The final fixture
  lint adjustment was rechecked separately (3 methods). SD failure-injection
  panic output is expected and remains asserted; no guard was relaxed.
- Normal and CONFIG_RELEASE=y **ARM/LTO full builds pass**, including the new
  integration gate and existing map/partition/heap, CRC, log-hash, resources and
  syscall checks. Normal build ID `d24e3773957df8abc96c12c93308d93f5214f778`;
  release build ID `411fcc60bf68ac4be617a0bcb0682c67b9e4d721`.
- A compatible CPU-metrics/reminder diagnostic configuration and both selected
  real ARM objects pass. Other diagnostic combinations retain their rules but
  were not all compiled or linked in this increment.
- Ruff check/format and git diff --check pass. No production shared/common C or
  CMake files changed. Numeric ABI, retained/partition layouts, timing constants,
  calibration, identity, Gabbro compatibility and ownership guards are unchanged.

Commands/logs are preserved under
`build-quality-20261121/build-integration-finding7/`, including baseline/final
Ninja compile databases and link commands, normal/release ELF/map/config/images,
actual wrapper manifests, hashes, source snapshots and result.json.
Build commands used the existing Arm GNU 14.2 / temporary Python environment:

```
PATH=/tmp/pebble-quality-venv/bin:/home/baz/pebble/build/arm_gnu_toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:/usr/bin:/bin \
  PYTHONPATH=tools/libs/pbl-cli CCACHE_DISABLE=1 \
  /tmp/pebble-quality-venv/bin/python -m pbl -b build-quality-20261121 build
```

The release directory was configured with `-DCONFIG_RELEASE=y`; the diagnostic
directory with `-DCONFIG_T5AI_TASK_CPU_METRICS=y -DCONFIG_T5AI_REMINDER_PROFILE=y`.
Focused test commands and repeatable build commands are in the integration doc.

Tooling/repair distinctions: the first configure-only fixture invocation found
an unusable cmake on the inherited PATH (PermissionError); selecting the existing
temporary tool environment fixed it. An initial validator prototype omitted
allocator wrappers by reading only the SoC/global contributed options; reading
the actual ELF target options fixed that validator bug. Neither was a runtime
regression. The console-disabled configure error and unused diagnostic variable
were actual pre-existing source/build defects, repaired without weakening guards.
Initial failure logs are preserved alongside the successful results.

Finding 7 is complete at host/build scope. **No hardware was flashed, reset,
provisioned or formatted**, and no hardware/power/soak qualification is claimed.
The installed sleep-retry image remains `31b50d165281fa78016a880573b910fefe8c69cf`.
Per the operator's section-by-section instruction, commit this increment and
pause. **Next unresolved item: finding 8**, separating production CP-awake sleep
from experimental modes while preserving the pinned protocol and transitions.


### Finding 8 progress — 2026-11-22

Reassessment: the existing production coordinator already enforced CP-awake
returns and retained its cancellation, cleanup and reset guards. No new claim
that production had entered CP low voltage follows from the original audit.
The maintenance problem was the cascading diagnostic decoder and bounded test
termination mixed into the cleanup worker. A non-test alarm client also had an
implicit CP-transition fallback when its production policy was absent.

Changes:

- CP now selects one complete named feature policy for each of the 27 existing
  sleep operations, under the existing successful-arm IRQ lock. It replaces 20
  interdependent mutable mode booleans; resource ownership, saved registers,
  cancellation and cleanup leases remain independent and unchanged. Unknown
  operations select no policy and cannot arm. No CP mode was removed or enabled.
- The qualified AP operation retains precedence over every diagnostic flag.
  Without that policy, CP-transition selection now requires the explicit TEST
  gate; otherwise preparation returns E_INVALID_OPERATION before sending a
  mailbox request. Protocol comments identify production operation 19 and
  experimental operation 3; all 102 numeric operation values and ABI are intact.
- Bounded diagnostic reporting, validation and termination now have a separate
  TEST-only coordinator helper. Its nine handled exits return to the worker at
  exactly the prior boundaries; ordinary production cleanup, counter guards,
  evidence/retry policy, timeouts, reset behavior and timing constants stay put.
- Policy reads always inline into CP ITCM callers; suspended-flash paths gain no
  out-of-line helper call. No shared/common production source changed.

Validation:

- Before edits, the existing four sleep suites passed **44 methods in 70.939 s**
  on the host with ASan/UBSan. After edits, the same suites passed **46 methods
  in 97.423 s**, including all 98 CP endpoint cases, 39 AP endpoint cases,
  cancellation, stale/future events, late replies, retained evidence, production
  state transitions and fail-closed cleanup. The final CP inlining and expanded
  AP guard cases passed focused reruns.
- New full-translation-unit coverage prepares/cancels every diagnostic, then
  enters CP-awake policy and proves zero CP resource, clock, mask and low-voltage
  transitions. Unknown wire values 102..255 and UINT32_MAX fail without arming.
  AP selection tests now compile the complete client instead of extracted
  functions; production precedence and absent-policy rejection are covered both
  with and without all diagnostic flags.
- An independent capture of the pre-refactor decoder matches **all 27 policies**,
  including analog and masked-CP-WFI exceptions. No test expectation was relaxed.
- Both old and new CP implementations compile in the complete vendor
  low_pwr_core.c translation unit, with hash-checked current overlay contexts,
  real SDK headers/configuration and Arm GNU 10.3. Candidate object text is
  **9390 vs 9638 bytes**, BSS **394 vs 409 bytes**, data **16 bytes unchanged**.
  Symbol/relocation inspection confirms no out-of-line policy helper.
- The complete diagnostic AP coordinator compiles with real Kconfig-generated
  headers and Arm GNU 14.2, -Werror and -fno-lto. Its temporary configuration
  explicitly disables the power-profile selector before selecting CP-transition
  diagnostics; the normal profile continues forcing CP awake. This is object
  coverage, not a complete diagnostic firmware link.
- The normal ARM/LTO firmware build and map/partition/heap, image integration,
  CRC, log-hash, resource and syscall checks pass. Build ID:
  `b51ab78563af81dfcb115117e17bc85ad6545b33`. Production .text size is unchanged
  at 1638080 bytes. The 928-byte SRAM .ramfunc section is byte-identical to the
  finding-7 baseline; retained data stays 136 bytes at 0x280103a0 and the AP
  trace stays 16 bytes at 0x28010440. Ruff and git diff --check pass.

Commands used the existing temporary Python/tool environment:

```
python -m unittest tools.tests.test_t5ai_native_alarm \
  tools.tests.test_t5ai_retained_sleep tools.tests.test_t5ai_sleep_guard \
  tools.tests.test_t5ai_diagnostic_config
PATH=/tmp/pebble-quality-venv/bin:/home/baz/pebble/build/arm_gnu_toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:/usr/bin:/bin \
  PYTHONPATH=tools/libs/pbl-cli CCACHE_DISABLE=1 \
  /tmp/pebble-quality-venv/bin/python -m pbl -b build-quality-20261121 build
```

Exact CP/AP object command arrays, configs, baseline policy capture, scripts,
logs, source snapshots and normal firmware outputs are preserved under
`build-quality-20261121/sleep-policy-finding8/`.

Tooling distinctions: LeakSanitizer failed inside the sandbox; the unchanged
baseline and final suites passed outside it with sanitizers still enabled.
The system clang-format could not parse the repository's newer style options;
23.1.5 was used. The first out-of-tree CP compile omitted its original source
header directory; restoring that include search path fixed the fixture without
changing production headers. The first diagnostic configuration correctly kept
WAKE_ONLY selected through T5AI_POWER_SAVINGS; explicitly choosing a diagnostic
profile resolved the configuration assertion without weakening that selector.

Finding 8 is complete at host/build scope. The CP candidate was compiled as an
object, **not fully linked, packaged, flashed or hardware-qualified**; the normal
AP build uses the existing pinned CP inputs. No new electrical, reconnect or
soak claim is made. No hardware was flashed, reset, provisioned or formatted;
installed image remains `31b50d165281fa78016a880573b910fefe8c69cf`. Layouts,
calibration, Bluetooth/Gabbro identity, storage contents and quarantine/ownership
guards are unchanged. The operator requested this increment be committed before proceeding to finding 9.
**Next unresolved item: finding 9**, mapping HCI work producers and evaluating
notification/deadline waits before changing qualified polling defaults.


### Finding 9 progress — 2026-11-22 (candidate, uncommitted)

Committed finding 8 as `8371a1b2a` after the operator requested its commit.
Its gitlint and staged/source checks pass; unrelated power and remaining-task
edits were excluded. Finding 9 is reassessed against that HEAD.

The producer map is complete in [HCI work and waits](hci-work.md). Controller
frames already woke the independent IPC task, and retained-sleep builds had
latched upper-task notifications. Configurations without retained sleep compiled
those notifications out. Reliable RX retention relied on the upper task's
periodic kick; event-pool frees have no general notification. Removing every
retry timer would therefore strand packets under host-pool pressure.

Implemented a board-local candidate:

- Existing upper work notifications now exist in every T5AI HCI configuration.
  Enqueue publishes under the TX mutex and signals after unlock. Frame routing
  signals both workers, and a late transport fault signals the upper task again.
  Even legacy non-retained builds now use a latched semaphore with the original
  5 ms timeout; the normal retained fallback keeps its 5/100 ms bounds.
- `CONFIG_T5AI_HCI_EVENT_WAIT` is **default off**, including in the qualified
  power profile. With it enabled, the upper task waits for notification, or the
  remaining 30 s profile deadline when profiling is enabled. The IPC worker
  waits indefinitely only when no reliable host packet is retained. Otherwise
  it retries after min(5 ms, remaining oldest-packet lifetime). The original
  250 ms receive lifetime, 2500 ms ACL packet deadline, 5 ms ACL retry and bounded
  send/recovery ownership waits remain intact.
- Found and reproduced a real pre-existing receive timestamp defect: uptime is
  64-bit but each received tick is 32-bit. Beyond the first 32-bit tick rollover,
  the old subtraction could immediately expire a newly retained packet. Elapsed
  comparisons now use the stored tick width, with no timestamp/layout change.
  The complete committed IPC translation unit aborts the new no-premature-expiry
  assertion; the candidate passes before, across and beyond rollover.
- Full-unit profile compilation exposed the stale `PebbleTask_KernelBackground`
  enum. The local profiler now samples `PebbleTask_SysWorkq`, preserving the shared
  task enum. Its stack-size printf cast also encloses the sizeof division, so the
  host's wider size_t does not create a format mismatch. Neither defect affects
  the previous default profile-disabled build.

Validation:

- Unchanged baseline: **9 HCI/native-object methods pass in 1.188 s**.
  Final HCI/native/configuration set: **22 methods pass in 11.273 s** with
  sanitizers. The expanded profile selector matrix then passes separately
  (1 method / 16 configurations, 4.549 s): retained sleep on/off, event wait
  on/off, HCI/flash profiling off, individually on and both on.
- New tests compile complete upper/IPC transport units, real kernel types/tick
  conversion, NimBLE mbuf code and MIPC definitions. They cover coalesced signals,
  publication at the blocking boundary, successful ACL enqueue ownership, actual
  IPC worker drain, buffer-pressure retries, one-tick remaining lifetime,
  timeout/fault wakeup and 32-bit received-tick rollover. Requested Kconfig
  values must take effect; no guards or ownership assertions were relaxed.
- A 60 s host wait-selector model reports **600** retained-fallback upper timer
  wakes, **12000** without retained sleep, and **zero** candidate idle timer
  wakes with profiling off. Signals before/at take are consumed in the same
  modeled tick. This is not measured watch scheduling, packet latency or power.
- Normal **ARM/LTO full build passes** all map/partition/heap, image integration,
  CRC, log-hash, resource and syscall gates. Event wait/profile remain off.
  Build ID `754fd0b3e9dd51d396f7e5cdb4059bb91adb8adc`.
- Isolated candidate **ARM/LTO full build passes** the same gates, with event
  wait and HCI profiling explicitly enabled. Build ID
  `37a2be6dcc819bbe6d55062419813135d31e577b`. This is a compile/package check,
  not a matched performance comparison: the normal build has profiling off.
- Both retain the 928-byte SRAM sleep section, 136-byte retained region at
  0x280103a0 and 16-byte AP trace at 0x28010440. Normal .text is 1638072 bytes,
  eight bytes smaller than finding 8; no shared/common production code changed.
  Ruff and git diff --check pass. Existing test fixtures were adapted to the
  new function argument/notification endpoint; new work tests use real headers
  and complete units rather than extracting functions.

Reproduction commands (with the existing temporary toolchain/Python PATH):

```
python -m unittest tools.tests.test_t5ai_hci_work \
  tools.tests.test_t5ai_hci_sleep tools.tests.test_t5ai_hci_acl \
  tools.tests.test_t5ai_hci_pressure tools.tests.test_t5ai_native_objects \
  tools.tests.test_t5ai_config
python -m pbl -b build-quality-20261121 build
python -m pbl -b build-quality-finding9-candidate configure --board unhygienix \
  -DCONFIG_T5AI_HCI_EVENT_WAIT=y -DCONFIG_T5AI_HCI_PROFILE=y
python -m pbl -b build-quality-finding9-candidate build
```

Logs, baseline reproduction script/assertion, source snapshots, exact command
record, normal/candidate ELF/map/config/images and hashes are frozen under
`build-quality-20261121/hci-work-finding9/`. Initial fixture failures (missing
mock type/endpoint, redefined real cache inlines and missing profiling endpoint
mocks) were host harness issues; the real enum/format and received-tick defects
are distinguished above. All were resolved without suppressing warnings.

**Finding 9 remains open** for matched on-device idle-wakeup and packet-latency
comparison, capacity pressure and reconnect/recovery checks before switching
qualified defaults. Do not advance to finding 10 or enable this option in the
power profile on host evidence alone. Leave the candidate uncommitted for review.
No hardware was flashed, reset, provisioned or formatted. No hardware, Bluetooth
or power qualification is claimed; installed image remains
`31b50d165281fa78016a880573b910fefe8c69cf`. AP/CP ABI, hardware timing constants,
retained/partition layouts, calibration, identity, Gabbro compatibility, storage
contents and ownership/quarantine guards remain intact.


### Finding 9 candidate installation — 2026-11-22, 11:28 UTC

The operator explicitly requested flashing the candidate. Rechecked host lab
(no configured watches), free CH342 UARTs and live console reception (176 bytes).
A fresh candidate build passed; the guarded flasher validated its current
phone metadata, vectors, CRC blocks and exact AP range before writing. The
candidate resources are byte-identical to the previously installed pack.

AP-only programming took **51.9 s**, range **0x132000..0x2e11e2 exclusive**.
Installed build ID **`37a2be6dcc819bbe6d55062419813135d31e577b`**, version
`v4.41.2-1656-g8371a1b2a-dirty`, protected SHA-256
`617c02879daa36ac263c6d5ac40e0a174ae07bbe62c038da6bdc584ab2c0724e`.
This image enables event waits and HCI profiling; source defaults remain off.
CP, system resources, PFS, calibration and pairing/identity storage were not
programmed. The item-8 CP policy source refactor remains uninstalled; its
behavior-preserving AP changes and item-9 AP candidate use the existing CP ABI.

First boot verified the exact build ID, AP/CP reasons **0/0**, contract **0x78f**,
existing SD PFS (`new=0`), valid resources and launcher. The existing bond was
restored with encryption and PPoGATT opened (reported 548 ms). At firmware time
11:29:10.245 the coordinator reported **32 AP-only returns**, with zero CP,
clock, mask, resource and fault counts. Two HCI heartbeats at 11:29:17.360 and
11:29:47.360 reported active=1, queue=0, started=1, fault=0. RX rose 480->488,
events 84->86 and ACL/commands 78->79; the busy-retry count stayed at 58.
No post-boot transport/sleep failure, panic or additional boot was captured.
Vendor ROSC/event-init/temperature boot warnings match the previous image's
record and are not evidence of a newly introduced HCI regression.

The capture is **stopped**, 4053 bytes over 167.221 s including programming;
first host-received boot bytes were at 11:28:37.174 UTC. Artifacts, sealed image,
matching dictionary, raw capture, exact flash log and result.json:
`build-quality-20261121/hci-work-finding9/flash-20261122/`.

The watch is left running this candidate. Source changes remain uncommitted.
This is bounded installation/startup evidence, **not** the matched device
idle-wakeup/packet-latency A/B, reconnect-cycle, power or soak comparison needed
to finish finding 9. Keep the remaining finding-9 checkboxes open and pause for
operator verification; no default switch or further hardware reset was done.


### Finding 9 phone-assisted A/B qualification — 2026-11-22

**Initial measured qualification was blocked; subsequently accepted by the
operator as recorded below. Uncommitted; event waits remain default off.** The
operator authorized ADB phone testing and flashing. Matched AP-only images use
identical source, retained sleep and HCI profiling; saved `.config` files differ
only in `CONFIG_T5AI_HCI_EVENT_WAIT`. A (fallback) is build ID
`4762101d711f74b2f6db465cb9c07a520502c219`; B (event waits) is
`76b2ff008a6341fc4d70605fbb7d90d778d92aa5`. CP, system resources, PFS,
calibration and identity/bonds were not programmed. The CP policy source
refactor remains uninstalled. No watch reset beyond the guarded AP flashes
was requested, and each captured flashed case contained one boot.

Profile-only counters now record upper/IPC semaphore signal and timeout
returns. They add no timer and no wire, retained or shared-code change. Both
matched ARM/LTO builds and the default profiling-off build passed existing
image/map/heap/partition/CRC/resource/syscall/integration guards. The default
`.text` remains 1,638,072 bytes; RAM functions remain 928 bytes, retained state
136 bytes at `0x280103a0`, sleep trace 16 bytes at `0x28010440`.

Validation commands/results:

- `python -m unittest tools.tests.test_t5ai_hci_work tools.tests.test_t5ai_hci_sleep tools.tests.test_t5ai_hci_acl tools.tests.test_t5ai_hci_pressure tools.tests.test_t5ai_native_objects tools.tests.test_t5ai_config`: 22 methods passed with ASan/UBSan before adding the final counter assertions.
- `python -m unittest tools.tests.test_t5ai_hci_work`: three methods passed after upper counter assertions, including the 16-configuration profiling matrix.
- `python -m unittest tools.tests.test_t5ai_hci_work.HciWorkTests.test_complete_ipc_profile_wake_counts`: passed for both wait policies, exercising the actual IPC worker, latched frames, retries and exact timeout counts.
- Ruff, Clang Format 23 and `git diff --check` pass. The sanitizer runs were host-escalated; no sanitizer was disabled.
- `pbl ... configure --board unhygienix -DCONFIG_T5AI_HCI_EVENT_WAIT=n/y -DCONFIG_T5AI_HCI_PROFILE=y`, then `pbl ... build`: both matched builds passed. Default was rebuilt with both flags `n`.
- Every hardware write used `tools/flash_unhygienix_firmware.py --raw ... --protected ... --tyutool /tmp/pebble-quality-tyutool --port /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00 --flash`. No older-version bypass, provisioning, formatting or resource/CP write was used.

Device evidence is under
`build-quality-20261121/hci-work-finding9/ab-20261122/`. It contains frozen
images/ELFs/maps/configs/dictionaries, exact source/test snapshots and hashes,
flash logs, received UART bytes with UTC chunk boundaries, targeted phone logs,
the original failed attempts, harnesses and per-case analyses.

| Run | Result |
| --- | --- |
| A fallback | Completed 10-minute connected idle, 200 serial echoes, 2,000 window-16 echoes and three full 67,600-byte screenshot transfers. All 25 verified UI Bluetooth off/on cycles returned a valid protocol echo within 60 s. |
| B event waits | First startup did not establish a phone session within 90 s. A phone adapter cycle restored an encrypted session only after the recovery harness's 60 s limit. Warm attempt recorded GATT/session failures and was stopped before claiming a matched comparison. |
| A2 exact fallback control | Reconnected without phone recovery. Completed two-minute idle, the same 2,200 echoes and three screenshot transfers, with no session closure or GATT discovery error. |
| B2 exact candidate repeat | Again failed the 90 s startup readiness limit; no connected latency/transfer qualification completed. |
| Af final exact fallback | Also failed the 90 s post-flash readiness limit. Its phone-adapter recovery missed 60 s too. This reproduces the startup failure with event waits **off**; it cannot be attributed solely to finding 9. The fallback remains installed. |

A's idle heartbeat samples spanned 570.267 s: 5,699 upper timeouts, 16 upper
signals and 5,730 IPC signals, about 10 timer wakes/s. Controller traffic was
16 frames, four events, zero ACL RX, four commands and no added send-busy
retry. A2's 90.034 s sampled idle span showed 899 upper timeouts and 907 IPC
signals. B/B2 disconnected samples showed the expected one profile-deadline
wake per 30 s; these are **not** a matched connected-idle or power comparison.

Performance varied even between fallback controls. A serial median/p95/max was
251.542/473.375/698.198 ms; A2 was 294.866/566.709/1418.990 ms. Their 2,000-echo
bursts took 37.472 and 103.250 s; screenshots took 8.459–8.787 and 26.688–31.133 s.
These round trips include ADB/WebSocket, Android, Bluetooth and watch scheduling;
they do not isolate internal HCI dispatch latency. No candidate throughput or
latency non-regression claim is supported by this run.

A's reconnect echo median/max was 36.785/54.121 s. Its full 25-cycle trace also
contains two transient GATT discovery failures and 27 protocol-session closures,
including the deliberately triggered closures. All cycles returned, but this
is not an error-free Bluetooth qualification. B recorded two GATT discovery
failures, three session closures, six LE disconnect records and 22 connection
parameter-request timeouts after its boot. HCI heartbeats remained
`started=1/fault=0`; no sleep/HCI guard fault or uncommanded AP reboot was seen.
Existing status/history records discovery failures in normal controls before
finding 9; no new root cause is established here.

Phone logs show a direct GATT attempt while flashing was still in progress,
Android status 133 and a pending `autoConnect` attempt. A phone-app reconnect
request still used `autoConnect`. First-failure bytes/phone state were preserved
before any recovery attempt. The first traffic request also started before the
phone reconnected; Android killed `svc bluetooth disable` with exit 137 without
disabling the adapter. Those two harness/tooling failures remain separately
recorded. The replacement reconnect harness observes the actual Settings switch
and adapter state before and after each toggle, without deleting bonds.

Initial A reported app UUID `16183a4f-c48d-4702-8a48-1211031a8115`; B and A2 both
reported launcher UUID `dec0424c-0625-4878-b1f2-147e57e83688`. The launcher is
therefore a comparison confound, not proof that the candidate lost a watchface.
No stored app was installed, removed or deliberately reselected. Screenshot
transfers prove protocol payload completion only; they do not qualify the panel
or watchface rendering. AppMessage/watchface-install regression, deliberate
pool-pressure injection, cold-power-cycle, current measurement and overnight
soak remain unperformed.

Final recovery: Af's verified phone adapter off/on cycle did not restore a
session within 60 s either. Restarting **only** `coredevices.coreapp` (force-stop,
start MainActivity; no app-data or bond deletion) then restored the encrypted
connection without another watch reset. The continuation `Ar/` contains zero
boot records, three valid echoes at 138.600/80.357/81.738 ms, version/launcher
queries and healthy connected heartbeats. The complete Ar capture received
1,872 bytes and stopped normally. Af remains the installed fallback image,
with event waits off, HCI profiling on and retained CP-awake AP sleep on.
All six UART captures/test processes are stopped and the task's ADB port forward
is removed. Source/input hashes match the measured builds; unrelated existing
`REMAINING_TASKS.md` and `power-todo.md` edits are unchanged. The machine-readable
`report.json` records every case, including original failures and continuations.
This establishes that phone-client state contributed to failed readiness;
it does not establish that every discovery/latency problem is phone-only.

### Finding 9 operator acceptance — 2026-11-22

The operator instructed: “auto reconnect is probably the phone. assume it is
for now and call this qualified”. Finding 9 is therefore qualified by explicit
operator acceptance, treating auto-reconnect as phone-side for now. This closes
the finding without establishing a phone-only root cause or a measured candidate
latency/throughput non-regression. The failed attempts and incomplete matched
comparison above remain part of the record; the sealed `report.json` and evidence
hash manifest retain their original measured results.

No default, installed image or guard changes accompany this disposition. Event
waits remain default off and the fallback Af image remains installed. The matched
connected-idle/latency comparison remains required before changing the default;
broader power, cold-boot, application and soak qualification is outside this
acceptance. No automatic commit.

**Next unresolved backlog item is finding 10:** graphics optimization boundaries
and measured benefits. Pause here for operator review before further changes.


### Finding 9 default enabled at operator request — 2026-11-22

The operator explicitly requested enabling event waits by default after
accepting finding 9. `CONFIG_T5AI_HCI_EVENT_WAIT` now defaults to `y` for
Unhygienix in both power profiles; `n` remains an explicit polling fallback.
HCI profiling stays default off. This supersedes the earlier hold on changing
the default, without claiming completion of the missing connected candidate
latency/throughput comparison. The installed Af fallback is unchanged; no flash
or commit accompanies this change. Validation results are recorded below.

Validation for the enabled default:

- `PATH=/tmp/pebble-quality-venv/bin:$PATH python -m unittest tools.tests.test_t5ai_config tools.tests.test_t5ai_hci_work tools.tests.test_t5ai_hci_sleep tools.tests.test_t5ai_hci_acl tools.tests.test_t5ai_hci_pressure tools.tests.test_t5ai_native_objects`: all 23 methods passed with ASan/UBSan, host-escalated. Configuration tests verify default `y`, profiling `n` and explicit fallback `n` in both power profiles; HCI tests retain both wait policies.
- `cmake -UCONFIG_T5AI_HCI_EVENT_WAIT -S pebble -B build-quality-20261121`: passed after removing the previous explicit `n` cache override. Generated `.config` has event waits `y` and HCI profiling unset, confirming the source default is effective.
- `cmake --build build-quality-20261121 -j 8`: ARM/LTO build passed, including image-layout, CRC, resource-size and syscall checks. Existing generated-declaration LTO warnings and serial LTRANS warning remain; no new warning cleanup is included.
- Ruff check/format with `--no-cache` and `git diff --check` pass. The first sandboxed Ruff invocation could not write its cache; rerunning without the cache passed. This was tooling access, not a code failure.
- Logs and generated configuration: `build-quality-20261121/hci-work-finding9/default-on-20261122/`. Historical A/B evidence is unchanged. Unrelated `REMAINING_TASKS.md` and `power-todo.md` hashes remain unchanged.


### Finding 9 commit checkpoint — 2026-11-22

The operator requested committing the complete finding-9 implementation,
including the enabled default, host harnesses and qualification record. The
23-method sanitizer suite and ARM/LTO default-on build passed before this
checkpoint; final Ruff, Clang Format and diff checks pass. Unrelated existing
`boards/unhygienix/tuya/REMAINING_TASKS.md` and `power-todo.md` edits are excluded.
No hardware write accompanies the commit. Next backlog item: finding 10.


### Finding 10, compositor increment — 2026-11-22

The three existing graphics tests passed against `2400c75b8` before production
changes (`python -m unittest tools.tests.test_t5ai_compositor_fast tools.tests.test_t5ai_pixel_fast tools.tests.test_t5ai_text_node_cache`). Inspection found
duplicated bitmap/text layouts and extracted-function or stub oracles, limiting
their fidelity. No rendering regression was demonstrated by that baseline.

The compositor harness now compiles complete production color bitblt, generic
bitblt, bitmap, geometry/color and local adapter translation units against real
headers and board Kconfig. The allocation declarations use the real generator;
only unused generated font IDs, host panic/log endpoints, app/legacy context and
the physical display declaration are fixtures. ASan/UBSan remain enabled.
Differential checks cover circular/rectangular rows, padded strides, reverse
row offsets, board-mask circular rows, clipping, offsets, supported compositing
modes, small geometry, sub-bitmap origins, self/partial overlap and legacy format
selection. The unmodified compositor passed the initial full-unit comparison.

The scoped implementation replaces the stock dispatcher's definition rename
with a named T5AI adapter at its two existing call sites. Common source files
remain unchanged and stock dispatch/fallback stays intact under LTO. Bitmap
bounds now supply dimensions; implicit circular rows additionally require real
board display dimensions. Regular 8-bit and board-mask row storage use a
constant-time extent bound; arbitrary circular row tables still require a scan
before writes to preserve non-overlap and unusual offset handling. The copy
clips intersecting spans to the operation's width.

Independent host compositor timing (Clang 21, O2, no sanitizers, 20,000 full
rectangular copies per sample, three samples): before, local/stock elapsed ratios
were 1.089/1.009/1.068; after, 0.966/0.950/0.933. This identifies the redundant
extent-scan cost and a small host benefit, not ARM/display timing, frame-rate,
power or hardware qualification. The option's existing policy is unchanged.

A newly exercised wide legacy fallback triggers UBSan in unchanged stock
`pebble/applib/graphics/8_bit/bitblt_private.c:369`: signed `1 << 31`. The real
stock dispatcher fails before the local adapter runs. The fixture retains
`LEGACY_WIDE` as an explicit expected-failure reproducer; ordinary differential
coverage separately exercises narrow legacy rows. No sanitizer is disabled and
no shared graphics code is changed to hide this dependency. Wide legacy
sanitizer qualification remains open; do not treat the expected failure as a
passing rendering test.

Compositor validation and next increment are recorded below. Finding 10's main
checkboxes remain open until all three optimizations and their performance and
freshness boundaries have been assessed. Changes are uncommitted.

Compositor increment validation: `python -m unittest tools.tests.test_t5ai_compositor_fast`
passes its ordinary differential test with one explicit expected stock failure.
`cmake --build build-quality-20261121 -j 8` passes ARM/LTO packaging, image-layout,
CRC, resource-size and syscall checks with compositor fast path enabled. Final
ELF `nm` contains both stock `bitblt_bitmap_into_bitmap_tiled` and the named
`t5ai_compositor_bitmap_into_bitmap_tiled` adapter. Generated Ninja definitions
redirect only bitblt.c/graphics_bitmap.c; stock dispatch is not renamed. Ruff,
Clang Format and diff checks pass. Evidence is under
`build-quality-20261121/graphics-finding10/`. Next: full-unit pixel equivalence
and an independent pixel benchmark, then text-cache freshness and timing.


### Finding 10, pixel increment — 2026-11-22

The old extracted-function pixel comparison passed, but the replacement using
complete production graphics_private.c, bitmap, geometry and color units
reproduces a real local defect: circular display-backed bitmaps legitimately
have `data_row_infos == NULL`; the fast path dereferenced that pointer at
pixel_fast.c:37. The real stock call succeeds first, then UBSan rejects the
local call. Original failure is preserved under `graphics-finding10/`.

The local fix delegates that layout to stock rather than duplicating the board
round-mask lookup. Existing pixel behavior, two-bit alpha truncation, clip
fallback and native explicit-row-table path remain intact. The new fixture
retains the previous exhaustive color-pair, opacity, format and random geometry
cases and adds display-backed circular rows using production types. No stock
source, display geometry, timing, ABI or memory layout is changed.

`python -m unittest tools.tests.test_t5ai_compositor_fast tools.tests.test_t5ai_pixel_fast`
passes both ordinary differential methods with the separately recorded expected
stock legacy-shift failure. Independent pixel host timing (Clang 21 O2, no
sanitizers, 5,000,000 mixed-opacity native plots/sample) measured local/stock
ratios 0.842/0.835/0.826. Each timed pair also compares final output bytes.
This is a host CPU benefit, not ARM/display qualification. Pixel option policy
is unchanged; ARM validation explicitly enables it in the candidate build.

The pixel definition rename remains necessary for the current scoped build
adapter: callers include graphics_private.c itself. A linker wrapper would
require removing its LTO or shared source changes. Neither is justified by this
cleanup; the local eligibility fallback is explicit and stock remains available.

Pixel increment ARM validation: configuring `build-quality-finding9-candidate`
with pixel/compositor enabled, text cache and HCI profiling disabled, then
`cmake --build build-quality-finding9-candidate -j 8` passed ARM/LTO and all
packaging/image/resource/syscall guards. Ruff and diff checks pass. No flash or
commit. Next increment: text-cache freshness against the production layout unit.


### Finding 10, text-cache increment — 2026-11-22

The replacement harness compiles the complete production cache and text-layout,
UTF-8, bidi, Arabic shaping, codepoint, hash and iterator units. Real
TextLayout/TextLayoutExtended/FontInfo/kernel types replace the previous
inaccurate duplicate layouts. Glyph resources use the existing test fixture;
font height, CPU privilege, thread/task identity, non-rendering window entry,
allocation and panic/log endpoints are host fixtures. This exercises real
layout/freshness logic, not real font resources, drawing or display scheduling.

The unchanged cache passed its full-unit baseline. The increment changes its
render boundary to named `t5ai_text_node_cache_window_render` entries in App and
round/rectangular transition callers, keeping stock window_render's definition
and kernel-modal callers untouched. No common source file changes. A local
header documents the privileged App render scope and mandatory extended-layout
contract for text_node.c. Per-render reset, capacity, allocation section and
existing counter policy are unchanged.

The differential checks include output/base/extended fields, cache hits proved
by skipped real layout work, per-render invalidation, owner/privilege/task
bypass, font/box/spacing/flow keys, paging/perimeter callbacks, overflow and
alignment modes, mutable strings, long-string bypass, eviction, multilingual
text and line breaks. No cache rendering defect is claimed from this review.
Independent host measurements (Clang 21 O2, fixture glyph metrics, 20,000 fresh
layouts/sample, three samples) show repeated-hit cache/stock ratios
0.051/0.053/0.053 and sustained-miss ratios 1.032/1.034/1.021. Benefits therefore
depend on reuse; misses have overhead. These host figures do not establish
on-watch timeline hit rate, ARM speed or UI improvement. Cache remains optional.

Benchmark reproduction: `python -m tools.tests.t5ai_graphics_harness compositor --benchmark`
(and `pixel` / `text_cache` respectively). Omit `--benchmark` for ASan/UBSan
comparison. Clang ignores GCC-only optimize attributes in host builds; ARM GCC
builds still use their existing attributes. Evidence and limitations must be
considered before interpreting host timing as a platform benefit.


Finding 10 validation checkpoint:

- `python -m unittest tools.tests.test_t5ai_compositor_fast tools.tests.test_t5ai_pixel_fast tools.tests.test_t5ai_text_node_cache tools.tests.test_t5ai_build_integration tools.tests.test_t5ai_config`: 17 methods, 16 pass and one expected failure preserving the stock legacy signed shift. The compositor methods were rerun after extending mode coverage through tint/luminance and retain the same outcome.
- Candidate configured with compositor, pixel and text cache `y`, HCI profiling `n`, event waits `y`; `cmake --build build-quality-finding9-candidate -j 8` passed ARM/LTO, image-layout, CRC, resource-size and syscall guards. Existing generated-declaration LTO warnings remain. Flash usage is 1,662,120 bytes; optional text-cache display-RAM use is 124,288/131,072 bytes, within its existing reservation.
- Final ELF contains stock `bitblt_bitmap_into_bitmap_tiled` / `window_render` and both named local adapters. Pixel stock fallback remains available. Ninja compile definitions redirect only the inventoried callers; stock compositor/window definitions are no longer renamed. The text-size adapter may inline under LTO.
- Final Ruff check/format, Clang Format and `git diff --check` pass. The host sanitizer runs were escalated; no sanitizer was disabled. Initial missing generated-header/link-endpoint probes were harness setup failures, not firmware regressions.
- Frozen source/tests, patch, logs, candidate config/ELF/map and hashes are under `build-quality-20261121/graphics-finding10/`. The earlier finding-9 sealed A/B evidence is unchanged. Unrelated `REMAINING_TASKS.md` and `power-todo.md` hashes remain unchanged.

**Next unresolved finding-10 work:** measure each option on the ARM watch under
representative UI/graphics traffic and real font resources, including clear/copy
costs, cache hit/miss rate, target frame timing and visual comparisons. Host
benchmarks alone do not qualify the platform benefit. The separate stock wide
legacy signed-shift defect also remains a documented dependency. Main benchmark
and acceptance checkboxes stay open; do not advance to finding 11 or claim
hardware qualification. Pause with the source changes uncommitted for review;
no flash, reset, provisioning or storage write was performed in this pass.


### Finding 10, authorized flash and ARM bench — 22 November 2026

This hardware pass supersedes the preceding no-flash checkpoint; no automatic
commit is authorized. Evidence is preserved separately under
`build-quality-20261121/graphics-finding10/device-20261122-1511/`, including
all failed attempts. Earlier finding-9 A/B images and seals are unchanged.

Added default-off `CONFIG_T5AI_GRAPHICS_BENCH`: one privileged App render runs
stock/local comparisons on two private 260-by-4 PSRAM buffers, mixed-opacity
pixel plots and real `GOTHIC_18_BOLD` font layouts. It uses production bitmap,
layout, clock and AON definitions (32,000 ticks/s), never the live framebuffer
or CP-owned storage. Copy/clear/pixel order alternates; text stock precedes
cached layout. Text checks returned size, while complete host comparisons
cover extended layout fields/freshness. No frame-rate claim follows from this
microbenchmark. Scratch is initialized after CP PSRAM bring-up, inside the
existing privileged cache scope. Diagnostic defaults and graphics defaults
are unchanged; the installed test explicitly enables all three optimizations.

Two diagnostic defects/dependencies were exposed rather than bypassed:

- Display profiling included the removed `system/profiler.h` API. Its local
  adapter now includes the real profiler header, declares real nodes and uses
  current count/cycle macros. Host checks use the real node type. Enabling
  display profiling together with the benchmark exceeded the existing 128 KiB
  reservation (133,056 bytes). The guard correctly rejected that configuration;
  display profiling was disabled for the bench, with no reservation/layout change.
- The first draw-profile image flashed successfully but stalled at the boot
  logo before any benchmark record (operator-confirmed). Frozen `bench/`
  evidence retains this actual regression. The local `sched_block` wrapper
  omitted the production scheduler's third interrupt-lock-key argument. It now
  includes `kernel.h`, derives the real-function declaration with `__typeof__`,
  and forwards the key in bypass and measured branches. A complete-unit host
  test verifies key, return value, lock balance and timing counters; the frozen
  original fails against the real signature. A draw-profile-off control
  (`isolated/`) booted and completed all 12 then-available samples. The repaired
  draw-profile-on firmware (`fixed/`) booted and completed all 15, including
  the added clear benchmark. This establishes recovery from the observed stall;
  it is not full qualification of every profiler hook.

Final installed AP build **ada42d32395354471f04da81ad21c9a5f0df53b3**,
version `v4.41.2-1657-g2400c75b8-dirty`, protected SHA-256
`a5cc76a850c003a966f027633dfa4b77b7ba4c92eedf95f998992e7e6b045270`.
Guarded AP-only flash completed in 52.1 s, physical range
`0x132000..0x2e2524` (exclusive). No CP, resource, calibration or PFS image was
flashed, no bonds were cleared, and storage was not formatted/provisioned.
Boot found existing PFS (`new=0`), valid system resources and 30 stored apps;
controller and stored peer identities were retained. AP reset source was 0x4f
following the authorized programming operation, with the existing awake-contract
revalidation; do not count that as an uncommanded run-time reboot.

Final three samples per case, CPU 240 MHz at start/end, AON elapsed ticks:

| Case | Calls/sample | Stock ticks (0/1/2) | Local ticks (0/1/2) | Median local/stock ratio |
| --- | --- | --- | --- | --- |
| Copy, 260 x 4 | 1,024 | 347 / 339 / 345 | 362 / 361 / 372 | 1.065 (6.5% more time) |
| Clear, 1,040 bytes | 1,024 | 860 / 858 / 858 | 116 / 115 / 114 | 0.134 (86.6% less time) |
| Mixed-opacity pixels | 100,000 | 2,196 / 2,197 / 2,191 | 1,480 / 1,485 / 1,477 | 0.674 (32.6% less time) |
| Repeated text hits | 128 | 1,371 / 1,384 / 1,487 | 96 / 122 / 171 | 0.088 (91.2% less time) |
| Cycling text misses | 128 | 1,608 / 1,599 / 1,607 | 1,733 / 1,643 / 1,637 | 1.028 (2.8% more time) |

All 15 checks returned `ok=1`; text counters were 399 hits / 369 misses /
0 bypasses / 2,304 bytes. Ratios are medians of paired sample ratios, not ratios
of medians. Small warm PSRAM buffers, three samples and scheduled/interrupt
elapsed time limit generalization. Copy has no demonstrated benefit in this
workload. Sustained misses have overhead. Do not retain/enable every path on
the strength of clear or text-hit results alone; representative full-frame
copy/clear geometry and real UI hit rates remain the next decision point.

Phone/UI evidence and failures are separate from CPU comparisons:

- Preflight and draw-profile-off control used the existing Ping app without
  installing/removing any app. The control confirmed Ping running, three pongs
  (336.7 / 228.1 / 354.5 ms) and a complete 67,600-byte 260 x 260 framebuffer
  screenshot matching preflight SHA-256
  `b6d5c82dbf2981eab6d81c1423280443ffbce119c80156457f5136f9e3b3eadc`.
  This is framebuffer evidence, not a physical-panel visual qualification.
  That control also reached 32 AP-only sleep returns with zero recorded sleep
  faults or CP/clock/resource transitions; it had existing animation-ID errors.
- The phone was force-stopped before the final flash and restarted after boot.
  A further phone-only restart restored encrypted bonding and PPoGATT at
  15:37:52 UTC, but connection-parameter update timed out and disconnected at
  15:38:12 (`0x0208`). Preserve this actual failure; phone-only causation is
  not proven. Final socket/protocol/screenshot checks did not complete.
  A final bounded 32-second readiness attempt found no connected watch. No
  watch reset, bond clearing or storage repair was used to hide this outcome.
  A later spontaneous reconnect at 15:40:31 also failed during service discovery
  after another parameter timeout at 15:40:39; the complete UART is retained.
- Early developer-server-not-ready attempts and the socket handshake failure
  are preserved as harness/connection failures, not successful device checks.
  The host Ruff cache initially hit the read-only sandbox; `--no-cache` passed.

Validation commands/results:

- `python -m unittest tools.tests.test_t5ai_draw_profile tools.tests.test_t5ai_config tools.tests.test_t5ai_display_profile tools.tests.test_t5ai_compositor_fast tools.tests.test_t5ai_pixel_fast tools.tests.test_t5ai_text_node_cache tools.tests.test_t5ai_build_integration`: 22 methods, 21 pass and one explicit expected stock legacy signed-shift failure; ASan/UBSan retained.
- `cmake --build build-quality-finding9-candidate -j 8`: final ARM/LTO build and
  existing image/resource/CRC/layout/syscall gates pass. Flash 1,666,608 bytes,
  RAM 96,544, display PSRAM 126,912/131,072, DMA RAM 14,976; no guard weakened.
- `tools/flash_unhygienix_firmware.py --raw .../fixed/pebbleos.bin --protected .../fixed/pebbleos.t5ai-crc.bin --tyutool /tmp/pebble-quality-tyutool --port /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00 --flash`: AP-only guarded success. Per-image configs, maps, ELF, dictionaries, source, raw UART/chunk timestamps and phone attempts are retained.
- Ruff check/format (`--no-cache`), Clang Format and `git diff --check` pass.
  Unrelated `REMAINING_TASKS.md` and `power-todo.md` hashes remain unchanged.

**Next unresolved:** representative full-frame/UI stock-vs-local comparisons,
including copy eligibility/cost and cache miss/hit rate; the final reconnect
failure prevents a complete final-image screenshot check. Preserve the stock
wide-legacy UB dependency. Main finding-10 benchmark/acceptance checkboxes remain
open; do not advance to finding 11 or claim broad hardware qualification.
Pause with all source changes uncommitted for operator review.

Final capture was stopped cleanly after 418.866 seconds and 6,979 received bytes.
There was one AP boot in the final image capture and no additional boot banner
after the authorized flash. Task-owned ADB forwarding was removed; the phone
app and Bluetooth remain enabled. No capture or soak is left running.

Operator requested committing this checkpoint and moving to finding 11.
The open finding-10 performance/qualification limits above remain open.


### Finding 11, guarded NimBLE placement — 22 November 2026

Finding 10 was committed as `e0985d9a5` at the operator's request; gitlint passed.
Its open performance and hardware limitations remain recorded above. This next
increment is uncommitted for review; no additional flash or hardware operation
was performed, and the installed finding-10 image remains unchanged.

Reassessed the actual upstream units, current ARM map and packaging gate:
`porting/nimble/src/os_msys.c` and `nimble/transport/src/transport.c` declare
private static backing arrays without a section/backing-storage hook for both
pools. `soc/t5ai/framebuffer.ld` still selects `.bss.os_msys_1_data` and
`.bss.pool_acl_buf`. The previous map checker accepted both renamed input
sections and an MSYS size change from 0x27ec to 0x27f0 on the actual ARM map.
This reproduces missing validation; it is not evidence of a current misplaced
pool or a Bluetooth regression. A rename could otherwise escape the selectors
and consume internal RAM, subject only to unrelated aggregate capacity guards.

Preserved upstream/shared code and existing runtime allocation. The SoC linker
fragment now brackets each pool contribution with boundary symbols and asserts
its pinned backing capacity: MSYS 10,220 bytes (35 x 292), transport ACL 9,600
bytes (32 existing blocks). These are capacity contracts, not duplicate runtime
structure definitions. Counts, block sizes, initialization, AP/CP copy boundary
and placement order are unchanged. A reviewed capacity/configuration change
must explicitly update the contract; it cannot silently move backing storage.

The board-specific image validator additionally requires exactly one allocated
input-object contribution per pool, exact size, matching linker boundaries,
ordered contiguous ranges in `.t5ai_board_cache`, no framebuffer overlap and
containment in the unchanged AP display-PSRAM reservation. GNU ld single-line
and wrapped entries, LTO objects and archive members are supported. A KEEP
selector or linker symbol without an allocated input contribution is insufficient.
Existing image/ownership/partition guards remain active. An adjacent stack-range
check uses equivalent `itertools.pairwise` traversal to satisfy current Ruff.

Validation completed:

- `PATH=/tmp/pebble-quality-venv/bin:$PATH python -m unittest tools.tests.test_t5ai_nimble_pools tools.tests.test_unhygienix_image tools.tests.test_t5ai_build_integration`: 15 methods pass. New tests preprocess/link the complete actual linker fragment with GNU ld and reject individually renamed, grown/shrunken or duplicate pools. Map fault injection also covers internal-RAM placement, overlaps, missing/mismatched boundaries and reservation overflow. Synthetic byte-array link fixtures test linker contracts; they do not model NimBLE runtime structures or qualify Bluetooth.
- Initial integration-test subprocesses could not execute the default PATH's `cmake` (PermissionError). The 12 map/linker/packaging methods passed; using the configured venv CMake resolved the tooling failure and all 15 passed. No firmware guard or test was weakened.
- `cmake --build build-quality-finding9-candidate -j 8` and explicit `python tools/check_unhygienix_image_map.py build-quality-finding9-candidate/pebbleos.map` pass. ARM/LTO, layout/CRC/resource/syscall checks remain active; existing generated-declaration LTO warnings remain. FLASH 1,666,608 bytes; RAM 96,544; display PSRAM 126,912/131,072; DMA 14,976. No memory limit changed.
- Before/after actual ARM map comparison confirms MSYS at `0x60010820`, size `0x27ec`, ACL at `0x6001300c`, size `0x2580`. Framebuffer/cache/PFS/DMA boundaries, heap start/end and the entire `.config` are unchanged. Linker boundary symbols/assertions add no backing bytes. Build metadata may differ; byte-identical firmware is not claimed.
- Ruff check/format and `git diff --check` pass. Evidence, pre/post maps/configs, ELF/protected image, source, test/build logs and hashes are under `build-quality-20261121/nimble-pools-finding11/`. Unrelated `REMAINING_TASKS.md` and `power-todo.md` remain untouched.

**Next unresolved finding-11 work:** replacing both private-name selectors with
an explicit supported upstream storage contract. Neither current unit exposes
that hook. Duplicating either complete upstream unit, intercepting generic pool
initialization, or injecting identifier/attribute substitutions would add a
larger private dependency. This increment uses the TODO's explicit interim
validation route; the first checkbox remains open. Review this independently
before any shared/upstream API change. No finding-12 work or automatic commit.

Operator requested committing the verified finding-11 guard increment and moving
to finding 12. The supported-storage-hook migration above remains open.


### Finding 12, native CO5300 panel state — 22 November 2026

Committed the verified finding-11 guard increment as `8782bc225` at the operator's
request; gitlint passed. Its explicit supported-storage-hook migration remains
open. Work moves to finding 12 in independently reviewable driver increments.

The existing panel/display-power/backlight baseline passed seven methods against
that commit. A separate real-header probe compiled the complete committed
`co5300.c` and supplied a second native instance to its init function. A hardware
endpoint requiring the second reset GPIO returned `-EXDEV` (-18): the committed
driver ignored the argument and used `CO5300.reset`. This reproduces the native
instance-routing defect; it does not demonstrate a failure on the current board's
single panel. Original source/header, probe, output and config are preserved.

`pbl_panel_co5300_state` now holds the existing desired/applied brightness,
prepared/valid/display flags and shared transaction mutex. The board creates
that state with `PBL_PANEL_CO5300_STATE_DEFINE` and attaches it to its const
native device. Panel callbacks recover the containing device from the class
argument; device init uses its actual device argument. Every reset and command
uses that instance's GPIO/SPI descriptor. No driver operation references the
board `CO5300` singleton. The local brightness entry now takes a panel argument.

Board-local backlight passes `&CO5300.panel`. The T5AI display's existing critical
sections use that panel's state mutex, preserving serialization with brightness
changes and completion outside the lock. Device metadata/dependencies, reset
pin 29, active polarity, SPI bus/mode/80 MHz setting, panel timing 466 x 466,
register values, command/error order and all 20/120/200 ms delays are unchanged.
No common Pebble, generic class or upstream source changed. Display conversion,
logical geometry, touch orientation, calibration and the installed dim/default
policies are unchanged. This panel-only increment does not make the T5AI display
controller a general multi-panel driver or alter its fixed conversion buffers.
Long busy waits are explicitly still finding 13.

Validation completed:

- CO5300 sequence/cache/sleep tests now compile the complete production unit
  with real production headers, types, native mutex/timeouts and generated
  Unhygienix configuration under Clang 21 + ASan/UBSan. No includes/functions
  are stripped. Hardware/mutex/delay calls remain modeled endpoints. Explicit
  sleep-disabled and sleep-enabled variants retain exact reset delays, all
  fourteen init/enable command failure sites, GPIO failures, desired/applied
  brightness freshness, repeated requests and sleep-error checks.
- New `test_t5ai_panel_instances` uses the real native state/device macros and
  two differently configured panels (distinct SPI bus/frequency/mode and reset
  port/pin/polarity). It deliberately defines no `CO5300` singleton. It checks
  supplied-device init/error routing, command traces, brightness/display/reset
  isolation and independent mutexes, with both sleep settings. An initial new
  fixture expected nine lock acquisitions for eight brightness calls; its
  bookkeeping was corrected to eight and each call now independently asserts
  one matching lock/unlock. This was a fixture-count failure, not a driver fault.
- `PATH=/tmp/pebble-quality-venv/bin:$PATH python -m unittest tools.tests.test_t5ai_panel_instances tools.tests.test_t5ai_panel_init tools.tests.test_t5ai_panel_power_cache tools.tests.test_t5ai_display_power tools.tests.test_t5ai_backlight tools.tests.test_t5ai_display_batch tools.tests.test_t5ai_display_class tools.tests.test_t5ai_display_recovery tools.tests.test_t5ai_display_rotation tools.tests.test_t5ai_display_buffer tools.tests.test_t5ai_display_worker tools.tests.test_t5ai_display_startup tools.tests.test_t5ai_board_psram`: all 16 methods pass. Existing display/backlight tests still use their older modeled class layouts; their endpoint wiring was adapted to the new panel argument/state-lock path without removing assertions. They are not recorded as full real-header integration tests.
- `cmake --build build-quality-finding9-candidate -j 8`: ARM/LTO and image-layout,
  resource-size, CRC and syscall guards pass; existing generated-declaration
  LTO warnings remain. FLASH 1,666,608 bytes, RAM 96,544, display PSRAM
  126,912/131,072, DMA 14,976 and PFS metadata 8,192, unchanged from the preceding
  build. Pinned partitions, CP ABI and retained/linker layout sources are untouched.
- Ruff check/format, Clang Format and `git diff --check` pass. Unrelated
  `REMAINING_TASKS.md` and `power-todo.md` hashes remain unchanged. Frozen
  source/tests, patch, baseline reproduction, build/config/map/ELF/images and
  logs/hashes are under `build-quality-20261121/panel-instances-finding12/`.

**Next unresolved:** CST92xx native-instance migration, including IRQ/work/timer
context and generation safety, board I2C/GPIO bindings and the exact existing
coordinate transform. Keep its recovery/ownership ordering intact. This panel
increment is uncommitted for review. No new hardware operation, flash, reset or
storage write was performed; the previously installed finding-10 image remains
installed. Host tests and compilation do not qualify the new source on hardware.


### Finding 12, authorized panel flash — 22 November, 16:15 UTC

At the operator's explicit request, flashed the exact sealed panel-increment AP
image from `panel-instances-finding12/`; its manifest and all 15 live review
source files matched before flashing. Source remains uncommitted. The lab
inventory was empty (no configured lab file); host enumeration verified the
CH342 if00/if02 ports, neither held by another process, and actual received
UART bytes were verified. No CP, resources, calibration or filesystem image was
flashed and no bonds were cleared or hardware formatted/provisioned.

Guarded AP-only flashing completed in **52.0 seconds**, physical range
`0x132000..0x2e2524` (exclusive). Installed build
**46c239607873ce2c9f770bc26a81fc34e6a5586a**, version
`v4.41.2-1659-g8782bc225-dirty`, protected SHA-256
`9540ef6846fa459996d7fedbd71c84bfdd81fcf71da6448773febfb47ec0a6c6`.
Raw SHA-256 `2841d5d41f244ac909305a007ceeb0e9b8453c72d06c1f55bf97b52b1056a84e`.
The existing draw-profile/one-shot graphics benchmark diagnostic configuration
is preserved: all three graphics options and HCI event waits enabled, retained
CP-awake AP sleep enabled, display/HCI profiling disabled. Diagnostic defaults
in source were not changed for this flash.

The received new build ID confirms the installed image. Boot revalidated the
existing awake contract after AP source 0x4f / CP source 0 (authorized programming
reset), found existing SD PFS (`new=0`), valid system resources and 30 stored apps,
and reached Launcher at 16:15:19. All 15 one-shot graphics samples completed
with `ok=1` at 240 MHz / production 32,000 Hz AON. At 16:15:42, the same image
reported 32 AP-only sleep returns with CP/clocks/masks/resources/fault all zero.
This is bounded startup/render/sleep evidence, not physical-panel visual,
energy, touch or broad UI qualification.

The phone companion was stopped during flashing and restarted after boot.
No connected watch appeared during the approximately 30-second readiness check;
phone logcat/readiness evidence is retained and Bluetooth remained enabled.
No additional watch reset or pairing repair was attempted. Buffered pre-flash
UART contains old-image connection failures and must not be attributed to the
new image. The final new-image capture contains one boot banner; there was no
additional boot banner during the observation. Vendor startup warnings and
service mutex timing messages remain in the complete trace.

Evidence: `build-quality-20261121/panel-instances-finding12/device-20261122-161404/` contains exact images/config/map/ELF/dictionary,
flash output, timestamped received bytes, decoded log, phone attempts and a new
hash manifest. Capture stopped cleanly after 165.901 seconds
with 4,513 bytes. No capture, soak or task ADB forwarding remains
active. Earlier source/hardware seals were preserved. Main finding-12 acceptance
and CST92xx native-instance migration remain open; no automatic commit.

Operator requested committing the flashed panel increment and continuing with
the remaining finding-12 CST92xx migration. Phone/visual limits remain open.


### Finding 12, native CST92xx instance and touch class — 22 November 2026

Committed the flashed panel increment as `c645d1134` at the operator's request;
gitlint passed. The installed panel image and its phone/visual limitations are
unchanged. This separate touch increment is uncommitted and has not been flashed.

The old touch/startup baseline passed 16 methods. Inspection confirmed a single
global enable/contact/generation state, work/timer objects, I2C/GPIO bindings and
reports routed through global `TOUCH`. This is an instance-ownership dependency;
this pass does not claim a new failure on the current one-touchscreen board.

The driver-specific native device now **embeds `struct pbl_touch`**, as other
native touch drivers do. `PBL_CST92XX_T5AI_STATE_DEFINE` retains the normal touch
class state/lock and adds private controller state. The generated public role
remains exactly `const struct pbl_touch *const TOUCH = &T5AI_TOUCH.touch;`.
Enable/disable reference counts, last sample, property transforms and reporting
still use the unchanged production touch class. No shared/class code is replaced.

GPIO, IRQ, I2C and coordinate configuration moved to the board's const device:
same I2C0 / address 0x5a, falling-edge INT 42, reset 43, GPIO flags, native 466 x
466 and logical 260 x 260. Both inversions are now expressed by existing touch
class properties. The driver scales first, using truncating integer arithmetic;
the real class then inverts. Every native axis value 0..465 reproduces the prior
`259 - floor(raw * 260 / 466)` result. Cached coordinates are pre-orientation;
initial cached values are chosen so an idle/error/palm report before any contact
still delivers logical (0,0). Liftoff/ACK-echo/palm retain the last coordinates.
Touch service ranges remain 260 x 260 and no second orientation transform was
found in consumers; shell info now truthfully reports both inversion properties.
New zero-sized configuration guards reject division-by-zero geometry before
touch I/O/work setup. Valid board initialization/dependency order is preserved.

Each instance owns its mutex, coalesced work, retry/watchdog objects and counters.
IRQ registration carries the actual device as its argument. Work callbacks
recover the owner from the embedded work member; retry and queued generations
remain checked under the same IRQ/hardware lock boundaries. Input listeners are
still called after releasing the I2C lock, with the generation rechecked after
palm delivery. No per-event allocation, queue-depth change, retry shortening or
new ownership/quarantine policy was introduced. Reset 5/50 ms, 20 ms active poll,
80 ms transfer retry, three-failure reset threshold, 1,000 ms recovery/watchdog
and idle backoff remain unchanged. Existing non-detection logging/init policy
and register-read/report ACK order are preserved.

Validation completed:

- Replaced empty-header/fake-layout touch scaffolding with the complete driver,
  complete unmodified `drivers/touch/touch.c`, real headers/native device and
  work/timeout types, generated board configuration and complete `kernel/tick.c`.
  No function extraction or source-include stripping. Clang 21 O1 + ASan/UBSan
  exercise modeled GPIO/I2C/IRQ/work/input endpoints. The I2C endpoint models
  `pbl_i2c_transfer`, retaining the real inline read/write API and message flags.
- All fourteen existing touch behavior methods retain their assertions, adapted
  to native instance state. Touch startup now exercises the complete init/class
  path rather than extracted retry code. Probe attempts/reset budget, IRQ storms
  during failed I/O, coalescing, lost edges, stale work/generation, palm ordering,
  reentrant disable, invalid reports, ACK echoes and idle backoff still pass.
- New two-device checks use distinct buses/addresses, GPIO/IRQ controllers,
  resolutions and inversion/swap properties. They verify class user counts,
  correct IRQ argument, queue/retry/reset/state isolation, and a canceled old
  retry preserving new-generation work. Another case exhausts every value of
  each native axis, checks initial idle (0,0), exact prior rounding, and invalid
  boundaries. A real-class palm listener drops its reference without deadlock
  or an extra raw sample. Reset-delay checks require the hardware lock and the
  correct reset level; zero geometry/controller-init errors fail before touch I/O.
- Broader pass: `python -m unittest tools.tests.test_t5ai_touch tools.tests.test_t5ai_touch_instances tools.tests.test_t5ai_sensor_startup tools.tests.test_t5ai_touch_queue tools.tests.test_t5ai_build_integration tools.tests.test_t5ai_config tools.tests.test_t5ai_panel_instances tools.tests.test_t5ai_panel_init tools.tests.test_t5ai_panel_power_cache tools.tests.test_t5ai_display_power tools.tests.test_t5ai_backlight tools.tests.test_t5ai_board_psram tools.tests.test_t5ai_nimble_pools tools.tests.test_unhygienix_image`: 57 methods pass. After adding real-class palm-listener coverage and strengthening reset assertions, the final focused touch/instance/startup run passes all 20 methods. Existing unrelated fixture limitations remain as previously documented.
- `cmake --build build-quality-finding9-candidate -j 8`: final ARM/LTO and layout,
  CRC, resource-size and syscall gates pass. Generated role C points to the
  embedded `pbl_touch`. FLASH 1,666,656 bytes (+48); RAM 96,576 (+32), with the
  existing minimum-heap guard intact. Display PSRAM 126,912/131,072, DMA 14,976
  and PFS metadata 8,192 are unchanged. Actual pre/post map comparison confirms
  unchanged retained, sleep-trace, framebuffer, board-cache, PFS and DMA section
  addresses/sizes. Pinned AP/CP ABI, partition/layout sources, calibration,
  Bluetooth identity and Gabbro compatibility are untouched.
- The complete touch translation unit also compiles with real ARM production
  options plus `CONFIG_T5AI_IO_PROFILE=1`, including the per-instance queued-at
  field. This is a compile-only optional-path check, not runtime diagnostic
  qualification or a fully linked alternate profile.
- Initial host fixture compilation tried redefining the real inline I2C helpers
  and collided with a local output-field macro; those were harness setup failures
  corrected at the transfer endpoint/fixture naming. A compile-command probe
  initially assumed GCC without Ninja's ccache launcher; direct GCC compilation
  resolved that tooling assumption. No target regression or guard relaxation is
  claimed from these failures. Logs are retained separately from passing runs.
- Ruff check/format, Clang Format and `git diff --check` pass. Unrelated
  `REMAINING_TASKS.md` and `power-todo.md` hashes remain unchanged. Sources/tests,
  before-state files, logs, generated roles, config/map/ELF/images and hashes are
  under `build-quality-20261121/touch-instances-finding12/`.

**Next unresolved finding-12 acceptance:** on-watch touch behavior after this
refactor: startup, corners/axes, drag/hold/liftoff, palm and disable/re-enable,
plus the existing fault/connection observation limits. Host/model checks do not
qualify hardware. Source is uncommitted for review; no flash, reset or storage
operation accompanied this increment. The prior panel-only image remains on
the watch. Do not advance to finding 13 before reviewing this increment.


### Finding 12 touch increment flashed and checked — 2026-11-22, 16:43 UTC

Installed exact sealed build **27131da0fc4e09587bb24de78e1b362f788c39fe**,
`v4.41.2-1660-gc645d1134-dirty`. Protected SHA-256
`ab045507fadf2632afbb56e95aacd0f4b96aeb4f307592863422bdb6b2b665ae`;
raw SHA-256 `d5f986289354293d4fbbae17e9669f27bd18d22f8be99727911317f25e06efed`.
Verified all 37 sealed evidence hashes and all 12 source snapshots against the
live tree before flashing. `pbl lab --json` has no configured inventory; host
CH342 if00/if02 enumeration and free-port checks supplied the actual ports.

- Guarded `tools/flash_unhygienix_firmware.py --raw CASE/pebbleos.bin --protected CASE/pebbleos.t5ai-crc.bin --tyutool /tmp/pebble-quality-tyutool --port /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00 --flash`: passed in 52.0 seconds; AP-only physical range 0x132000..0x2e2546 exclusive. No CP/resources/PFS/calibration programming, bond clearing or formatting.
- Captured actual if02 UART bytes at 460800 and decoded with the candidate dictionary. New build reaches Launcher, existing SD PFS `new=0`, valid resources and 30 apps. All 15 graphics output checks pass. AP-only sleep records 32 returns with `fault=0`; one commanded-flash boot, no subsequent AP boot/assert/hard-fault observed.
- Operator reports **all interactions work normally** for four-direction navigation, corner taps, drag/hold/liftoff, palm cover and recovery. Separately reports **disable/re-enable passes** using Settings → Display → Touch and buttons. Five post-release draw-profile batches corroborate physical-input/render activity; they do not expose exact coordinates. Host exhaustive-axis and two-instance checks remain the coordinate/isolation evidence.
- Phone companion restarted after flash. Initial connection disconnected with reason 0x213, then successfully reconnected without another watch reset or bond clearing. Live checks pass three pongs, expected version, Ping app state and full 260×260/67,600-byte screenshot; screenshot SHA-256 `b6d5c82dbf2981eab6d81c1423280443ffbce119c80156457f5136f9e3b3eadc` matches the earlier capture. First connection trace/logcat preserved; this is not 25-cycle or cold-boot qualification.
- **Retained limitation:** at 16:43:25.511, one `CST92xx report transfer failed (1 total)` log precedes successful subsequent interactions. No later failure log appears, but the driver logs first/every 64, so an exact final failure count cannot be inferred. No induced hardware I2C fault/reset qualification or causal attribution to this refactor. Monitor this warning in later touch/I2C work; do not suppress it. Two `Animation 2 does not exist` errors also occur; identical errors exist in the earlier graphics control capture. Existing bootloader/CP startup diagnostics are retained.
- Evidence: `build-quality-20261121/touch-instances-finding12/device-20261122-164153/`, including flash log, raw UART, timestamps, decoded log, phone data/screenshot, operator record and separate hardware SHA-256 manifest. Capture stopped cleanly; phone forward removed, companion left running. Unrelated planning-file hashes unchanged.

Finding 12 targeted acceptance is complete: real-class two-instance isolation,
sequence/layout checks and operator hardware behavior pass. This short bench
is not a broad hardware or overnight-soak qualification. Source remains
uncommitted; paused for review. Next ordered item is **finding 13, scheduler
sleeps for long panel delays**. Historical unflashed statements above describe
the preceding source checkpoint.


### Finding 12 committed; finding 13 panel delays in source — 2026-11-22

Committed the verified touch increment as **41ee66f80**
(`t5ai: bind CST92xx touch to native device instances`), with DCO and
`Assisted-by: LLM`; gitlint passes. Only its twelve reviewed files were staged.
Unrelated `REMAINING_TASKS.md` and `power-todo.md` remain untouched.

Reinspection of current HEAD confirms finding 13 still applies: CO5300 prepare
spins for 20 + 200 + 120 + 200 ms; optional sleep-in spins for 200 ms on success.
`kernel/init.c` runs `pbl_device_init_all()` in KernelMain after scheduler start.
Display initialization calls prepare through the normal device/class path;
boot splash's explicit device initialization also runs from thread context.
The optional reprepare/unprepare calls in the local display enable/disable
callbacks hold the native panel mutex, with IRQs/preemption enabled. SPI
transactions return before each delay; the SPI transaction lock is not kept
across a panel timing wait. Display transfer-error recovery retries transfer
work after its existing one-second sleep; it does not itself reset the panel.

The local CO5300 helper now uses **existing `pbl_sleep_us()`** once the scheduler
starts. Complete production `kernel/time.c` rounds up and adds a tick for the
partial tick in progress: direct `PBL_MSEC`/`pbl_thread_sleep` would not provide
that minimum-delay guarantee. Existing 20/120/200 ms constants, reset levels,
command/value order, error exits and per-panel serialization remain unchanged.
Before scheduler start, retain the existing hardware-timer `delay_us()` path.
After start, the native assertion enforces non-ISR, unlocked native IRQ and
running/unlocked scheduler context; the device header documents that interrupts
and preemption must be enabled. No microsecond bus timings, shared/common kernel
code, board defaults, CP, partitions, retained layouts or identity were changed.

Validation and evidence:

- Current-HEAD baseline: `ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest tools.tests.test_t5ai_panel_instances tools.tests.test_t5ai_panel_init tools.tests.test_t5ai_panel_power_cache tools.tests.test_t5ai_display_power tools.tests.test_t5ai_backlight -v`: 8 methods pass. The new timing fixture also compiles the preserved complete committed unit, then fails its no-busy-wait-in-thread-context assertion as expected; this is an executed behavior reproduction, not a compile failure.
- Final run: the same panel/display/backlight modules plus `tools.tests.test_t5ai_panel_delays`, `tools.tests.test_t5ai_touch`, `tools.tests.test_t5ai_touch_instances`, `tools.tests.test_t5ai_sensor_startup`, `tools.tests.test_t5ai_board_psram`, and `tools.tests.test_unhygienix_image`: **41 methods pass** under the existing sanitizer harnesses. Full panel and `kernel/time.c` translation units use real headers/types and board Kconfig. GPIO/SPI and scheduler execution remain modeled.
- New cases verify exact reset/command order, minimum elapsed delays (including fractional tick phase), native sleep requests instead of busy waiting, early fallback, a held panel transaction mutex, all three reset-write and thirteen prepare-command failure exits, optional sleep-in on/off and failed-command behavior. Post-start ISR/native IRQ-lock/scheduler-lock calls enter the real always-enabled fatal guard; expected-panic checks require execution to return their unique sentinel, so compile failures cannot satisfy them. Existing instance and applied-state tests retain their assertions.
- `PATH=ARM_TOOLCHAIN:/tmp/pebble-quality-venv/bin:$PATH cmake --build build-quality-finding9-candidate -j 8`: ARM/LTO, image-map, protected CRC, resource-size and syscall gates pass. FLASH 1,666,752 bytes (+96 against the flashed touch image); RAM 96,576 unchanged. Display PSRAM 126,912/131,072, DMA 14,976 and PFS 8,192 unchanged. Actual map comparison confirms all six retained/reserved section addresses and sizes unchanged. Candidate config is byte-identical to the touch checkpoint. Existing generated-declaration LTO warnings remain.
- Complete panel ARM compilation with actual Ninja production flags plus `CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE=1` passes. This is optional-path compile coverage, not a full alternate-profile link or hardware qualification; the current board default leaves this flag off.
- Ruff check/format with `--no-cache`, Clang Format 22, `git diff --check` and gitlint pass. Initial system Clang Format 18 cannot parse this repository's format settings; cached Ruff cannot write in the read-only sandbox. Using the existing compatible formatter and cache-free Ruff resolves these tooling failures without source/test guard changes.
- Logs, original panel source, full review snapshots, exact candidate config/ELF/map/dictionary/images, layout comparison, optional compile command and SHA-256 seal: `build-quality-20261121/panel-delays-finding13/`.

**Next unresolved item remains finding 13 acceptance:** measure actual on-watch
startup and wake/reprepare responsiveness and verify panel minimum timing/
recovery with real scheduler/power behavior. Modeled timing checks prove the
sleep conversion and sequencing, not physical timing or scheduling progress.
No finding-13 flash or hardware operation was performed; the verified finding-12
touch image remains installed. Finding 13 source is uncommitted for review;
do not advance to finding 14 before completing this increment's validation.


### Finding 13 committed; finding 14 event/deadline battery delivery — 2026-11-22

At the operator's explicit request to commit and move on, committed the finding-13
source increment as **c4258ad88** (`t5ai: sleep through CO5300 reset and power delays`).
DCO/AI trailers and gitlint pass; only its eight reviewed files were staged.
Its on-watch startup/wake acceptance remains open. Advancing source work to 14
does not imply that the unflashed panel-delay candidate was hardware-qualified.

Current-HEAD reinspection confirms finding 14: charge IRQ and ADC completion
only set flags, and a permanent 1 Hz high-work callback polls them for normal
service delivery. The existing asynchronous ADC request, 30-second startup
holdoff after demand, 60-second sample freshness, five-second failure retry,
400 ms charge debounce, calibration conversion/range guards and terminal
SARADC quarantine rules remain appropriate and are preserved.

The board-local battery driver now schedules the earliest actual notification,
charge-debounce or demanded retry/startup deadline. ADC completion schedules
immediate high work that queues normal service work; synchronous logs, event
publication and `battery_state_force_update()` still run off the mailbox owner.
Charge edges restart the 400 ms deadline. A 30-second GPIO-only reconciliation
checks for missed edges without starting ADC work or idle normal callbacks.
If IRQ request/enable fails, retain the original one-second GPIO reconciliation
fallback. A missed edge with a working IRQ can now take up to 30 seconds plus
400 ms to reconcile; genuine IRQ delivery uses the 400 ms deadline directly.
This is the explicit safety-net latency tradeoff, not a claim of equal missed-edge
latency. No default ADC sampling interval or charge/USB proxy meaning changes.

Queued/running service work coalesces current flags, then reschedules flags that
arrive during delivery. Consuming a due retry prevents an immediate high-work
loop while the battery service schedules its own asynchronous request. If the
normal-work call pool refuses delivery, retain the flags and retry after 50 ms;
do not spin, drop completion or log on the mailbox owner. GPIO read errors do
not publish a false charge transition; a pending debounce retries instead.
GPIO sampling and edge timestamps share the IRQ lock; T5AI GPIO reads are
nonblocking register reads. Timers are initialized before enabling IRQs, and
an edge during enable is scheduled after initialization completes.

Validation:

- Committed baseline: `ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest tools.tests.test_t5ai_battery tools.tests.test_t5ai_saradc -v`: **14 methods pass**. The preserved complete committed battery unit also compiles with the new real-header fixture, then fails the idle servicing budget assertion during execution. A matched 60-second model records control **60 callbacks / 61 GPIO reads** versus candidate **2 callbacks / 3 GPIO reads**; both request zero ADC samples and zero service updates. These are driver/model counts, not electrical power measurements or whole-system wake counts.
- Replaced the battery fixture's copied GPIO/IRQ/work/event/ADC layouts and synthetic event constant with real production headers, Kconfig, `PebbleEvent`, native descriptors and complete `kernel/tick.c`; the complete driver is included without source stripping or function extraction. Hardware, queue execution, service and SARADC endpoints remain modeled. Production shared/common code is untouched.
- All six existing battery methods retain demand, freshness, range, asynchronous completion, failure/retry, terminal quarantine, coalescing, charge bounce and logging assertions. Their scheduler advances follow the new actual 50 ms delivery retry and 30-second healthy-IRQ reconciliation deadlines. The old single-periodic-registration assertion becomes a two-initialized-timer/idempotent-init check. These changes express the intended schedule; validity/error assertions were not weakened.
- Twelve new methods cover idle servicing, exact startup/debounce/completion deadlines, late first demand, queued-service bounce, work-pool pressure, retry-loop avoidance, GPIO errors, reentrant IRQ/completion delivery, IRQ-enable initialization races, request/enable failure fallback and 64-bit uptime. Native battery fixtures run under ASan/UBSan. The existing SARADC transport suite remains unchanged and modeled; no new CP or hardware qualification is claimed.
- Final command: `PATH=/tmp/pebble-quality-venv/bin:$PATH ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest tools.tests.test_t5ai_battery tools.tests.test_t5ai_battery_events tools.tests.test_t5ai_saradc tools.tests.test_t5ai_panel_delays tools.tests.test_t5ai_panel_instances tools.tests.test_t5ai_panel_init tools.tests.test_t5ai_panel_power_cache tools.tests.test_t5ai_display_power tools.tests.test_t5ai_backlight tools.tests.test_t5ai_touch tools.tests.test_t5ai_touch_instances tools.tests.test_t5ai_sensor_startup tools.tests.test_t5ai_board_psram tools.tests.test_unhygienix_image tools.tests.test_t5ai_config tools.tests.test_t5ai_build_integration -v`: **81 methods pass**.
- `PATH=ARM_TOOLCHAIN:/tmp/pebble-quality-venv/bin:$PATH cmake --build build-quality-finding9-candidate -j 8`: full ARM/LTO, image layout, CRC, resource-size and syscall gates pass. FLASH **1,667,136** bytes (+384 against finding 13); RAM **96,640** (+64). PSRAM 126,912/131,072, DMA 14,976 and PFS 8,192 remain unchanged. Actual map comparison confirms all six retained/reserved section addresses and sizes unchanged; candidate config is byte-identical to finding 13. Existing generated-declaration LTO warnings remain.
- Complete panel timing source stays committed; the battery profile diagnostic branch also compiles as a complete ARM unit using actual Ninja flags plus `CONFIG_T5AI_BATTERY_PROFILE=1`. This optional-path compile is not an alternate full build/runtime qualification. No AP/CP ABI, partition, calibration, Bluetooth identity, storage contents, ownership/quarantine or Gabbro changes.
- Initial fixture logging-matrix failures exposed real Kconfig selection/dependency rules: quiet-success is selected by the savings umbrella, and power profiling requires tick profiling. The matrix now uses the documented umbrella opt-out, enables the actual dependency and asserts effective symbol values. A fixture-only IRQ-error return typo was corrected. A broader run's CMake lookup failed before configuration; adding the installed tool directory to PATH makes all tests pass. These are fixture/tool setup failures, not evidence of target regressions; failed logs remain separate.
- Ruff check/format, Clang Format 22, `git diff --check` and gitlint pass. Unrelated planning-file hashes remain unchanged. Exact before/source snapshots, baseline and failure/passing logs, matched idle counts, default candidate config/map/ELF/dictionary/images, optional compile command and SHA-256 seal: `build-quality-20261121/battery-events-finding14/`.

**Remaining finding-14 acceptance:** on-watch charge/ADC delivery and idle-wakeup
observation with the real scheduler/IPC and charge wiring. The host model does
not measure energy, genuine missed IRQs or physical charger bounce. Finding 13's
panel startup/wake bench also remains open; the candidate contains both changes
relative to the installed touch image. No new flash/reset/provision/format was
performed. Finding 14 is uncommitted for review; finding 15 has not been started.
The flash/SARADC initialization coupling is intentionally left for finding 15.


### Findings 13/14 flashed and bounded bench — 2026-11-22, 17:20–17:33 UTC

At the operator's request, installed the exact sealed combined default AP build
**80136932e2a39db7789df18cfc8c67f37424debd**,
`v4.41.2-1662-gc4258ad88-dirty`. Raw SHA-256
`26b86f69f65ac1b60cb6a70d36c505ea8d42164c00036d6f6341c213516770f1`;
protected SHA-256
`8db1a5ea0d18e28e78b7d0ab6e3c0b33cff58c81184280d88d9fd23735f2cdec`.
This is now the installed image, including committed finding 13 and uncommitted
finding 14. All 33 original evidence hashes and eight live source/review files
matched before flashing. `pbl lab --json` has an empty inventory; actual host
CH342 if00/if02 enumeration and free-port checks supplied the UARTs.

Three guarded AP-only flashes were performed: default (52.1 s), temporary
battery diagnostics, and restoration of that exact default (52.2 s). Each used
`tools/flash_unhygienix_firmware.py --raw CASE/pebbleos.bin --protected CASE/pebbleos.t5ai-crc.bin --tyutool /tmp/pebble-quality-tyutool --port /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00 --flash`
with the corresponding stage's files. Default physical range is
0x132000..0x2e2744 exclusive. No CP, resources, PFS, calibration or bond data was
programmed/cleared, and no format/provisioning operation occurred. Only the
commanded flash boots were seen; no additional AP boot/assert/hard fault was
observed in the bounded captures. Buffered pre-flash UART is retained and
excluded from new-image analysis at each boot banner/build ID.

**Finding 13, default-profile panel bench:** all three boots reach Launcher and
complete all 15 graphics output checks. Initial default trace reaches Launcher
at 17:20:52.834 (RTC startup anchor 17:20:41.000); restored default reaches it at
17:30:11.167 (anchor 17:29:57.000). These are coarse boot-progress observations,
not matched panel latency or energy measurements; storage startup varies.
The operator reports **all five dim → wake → swipe cycles pass**, including
normal UI/navigation, without a freeze, corruption or noticeably longer pause.
This covers the default startup/dim path. `T5AI_AMOLED_SLEEP_ON_DISABLE` remains
off: ordinary dim/wake does not exercise optional sleep-in/reset/reprepare.
Minimum-delay and all command/error-exit checks remain the complete-unit host
coverage. Actual SPI-fault recovery, optional reprepare timing and timing-analyzer
measurements are unverified, so full acceptance is not checked off from this run.

**Finding 14, ADC delivery bench:** operator confirms the battery is disconnected
and explicitly requests keeping it disconnected. Watch power stayed connected.
A temporary full ARM build enables only existing `CONFIG_T5AI_BATTERY_PROFILE=y`;
all other effective config lines match, and image/layout/resource/syscall gates
pass. Diagnostic build **76f5377c89f7edaa84e0c735e395a3f48c83bff3** logs:

- 17:27:13.020: raw 2180, 4001 mV, elapsed 7 ms, CHRG proxy 1.
- 17:28:13.026: raw 2180, 4001 mV, elapsed 5 ms, CHRG proxy 1.

These verify real SARADC completion and deferred normal-work logging/delivery,
with 60.006 s between samples and continued phone responsiveness. The startup
sample appears after the existing holdoff; no ADC failure log appears. With no
cell, this is a charger-node reading, **not a qualified cell voltage or evidence
of a genuine charging transition**. An absent cell can leave an in-range reading;
the existing range guard is not cell-presence detection. Hardware charger edges,
bounce/missed IRQs, ADC error retries/quarantine and whole-system idle wakeups/
energy are not qualified here. Host cases still cover those error/scheduling
contracts; the 60→2 idle-callback result remains a model result.

Restored the original default configuration by removing the temporary override,
rebuilt successfully, and verified both raw/protected outputs byte-identical to
the original sealed candidate before final default installation. Diagnostic
logging is off in both the installed image and restored build workspace.
Default and restored-default traces each report 32 AP-only sleep entries with
`fault=0`; this is bounded sleep evidence, not a power/overnight qualification.

**Phone and first failure:** initial default and diagnostic images each pass
three pongs, version/app queries and a complete 260×260 / 67,600-byte framebuffer
read. After default restoration, three pongs and version/app queries pass, but
the framebuffer transfer fails: 17:31:07.116 UART records send-buffer shortage
for 1024 bytes on endpoint 8000 and early screenshot termination, followed by
connection-parameter retries reaching their bound. The host raises a screenshot
`TimeoutError`. This first failure, raw UART and phone logcat remain preserved.
A subsequent connection-only check passes three pongs (about 692–1012 ms) and
version without resetting the watch or clearing bonds. Do not claim a clean
final screenshot/transport qualification or attribute the failure to the phone
or these changes without further evidence.

Retained ancillary observations: the initial default logs one CST92xx report
transfer failure, as the prior touch checkpoint did; logging is rate-limited,
so the final exact count is unknown. Animation-not-found errors also occur,
with similar errors in previous control captures. They are not suppressed or
used as proof that finding 13/14 introduced a regression.

Evidence is under
`build-quality-20261121/battery-events-finding14/device-20261122-171934/`, with
separate default/diagnostic/restored-default raw captures, timestamped chunks,
exact images/dictionaries, flash/build/config logs, phone results, operator
record, rebuild-equivalence proof and an independent hardware SHA-256 seal.
Captures stopped cleanly at 229.112 / 183.176 / 247.416 s with 6017 / 5103 /
5052 received bytes respectively. Both UARTs are free, phone forwards removed,
and companion left running. Unrelated planning-file hashes remain unchanged.

**Next unresolved acceptance:** real-cell charge transitions and measured idle
behavior for finding 14; optional panel reprepare/fault timing for finding 13;
and preserved screenshot/connection-parameter failure investigation. Both source
changes are now installed and the bounded passed checks above are recorded,
but the full acceptance checkboxes remain open for those explicit limits.
No source fix or new commit was made during this flash/bench task. Finding 15
has not been started.


### Finding 14 committed; finding 15 independent IPC clients — 2026-11-22

At the operator's request, committed finding 14 as **77ccc46e2**
(`t5ai: deliver battery updates from events and deadlines`), with DCO/AI trailers
and passing gitlint. Only the eight reviewed finding-14 files were staged.
The combined finding-13/14 bench and its limitations above remain unchanged:
the battery stays disconnected, real-cell/idle-energy checks remain open, and
the screenshot send-buffer failure is preserved. No new hardware action was
performed during this increment.

Reinspection of committed HEAD confirms the finding-15 dependency: flash init
registers the SARADC notification route and returns its error before opening the
independent flash connection. The complete committed flash unit, compiled with
the new fixture, fails an executed success assertion when that route is made to
fail. This is an actual storage-startup dependency, not an environment failure.

Removed the SARADC include/call from flash initialization. Successful repeated
flash initialization now reuses the existing connection; an established socket
that has been quarantined remains unavailable. Definite connect rejection can
still retry through the transport. Missing replies still poison the socket and
cannot be retried into reuse. SARADC already owns idempotent route setup and
calls it lazily from each acquisition; its production implementation is unchanged.
Shared MB-IPC setup stays in the transport. Only the flash driver and client API
comments change; no shared/common production code changes were required.

Socket order: normal serialized startup still initializes flash before the first
battery acquisition. The transport has two slots and routes by CPU/port tuple,
not array index. Both sequential allocation orders work with pinned flash ports
16/1 and SARADC ports 17/2. Concurrent first-time transport/client initialization
is not newly guaranteed or tested. Do not clear a poisoned slot to recover a
client; storage/ADC lifetime ownership and terminal quarantine remain unchanged.

Validation and evidence:

- Baseline command: `ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest tools.tests.test_t5ai_saradc tools.tests.test_tuya_flash tools.tests.test_t5ai_flash_read_lease tools.tests.test_t5ai_flash_completion -v`: **18 methods pass**.
- Six new `tools.tests.test_t5ai_ipc_init` methods use complete production flash, SARADC and MB-IPC units with real headers, Kconfig, kernel synchronization types, timing conversion, private wire helpers and `kernel/tick.c`. Only private symbol names are disambiguated to combine units; functions are not extracted or stripped and wire/type layouts are not copied. CP acknowledgements, route registration, IRQ/kernel endpoints and scheduling remain modeled.
- ASan/UBSan cases cover ADC-route failure/retry without blocking flash, shared-transport route failure/retry without recreating synchronization, definite flash rejection/retry, ambiguous connect quarantine, repeated success and later flash quarantine, both allocation orders, and ADC timeout without losing flash availability. ADC connect is deliberately left pending for timeout checks; existing SARADC tests cover acquisition/notification behavior.
- Focused command: `PATH=/tmp/pebble-quality-venv/bin:$PATH ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest tools.tests.test_t5ai_ipc_init tools.tests.test_t5ai_saradc tools.tests.test_tuya_flash tools.tests.test_t5ai_flash_read_lease tools.tests.test_t5ai_flash_completion tools.tests.test_t5ai_flash_cache tools.tests.test_t5ai_flash_priority tools.tests.test_t5ai_battery tools.tests.test_t5ai_battery_events tools.tests.test_t5ai_hci_work tools.tests.test_t5ai_hci_sleep tools.tests.test_t5ai_hci_acl -v`: **53 methods pass**.
- A broader run replacing the three HCI modules with `tools.tests.test_t5ai_mailbox_failures` passes **59/60** methods. The remaining `test_hci_exhaustion_does_not_wait_for_logger` fails to compile: its extracted HCI helper calls `t5ai_hci_notify_work()` without a fixture declaration/implementation. Reproduced identically using all three inputs from committed HEAD in a temporary tree. This is a preexisting harness dependency, not a target regression or a clean broad-suite pass. The complete HCI work/ACL suites above pass. **New dependency for finding 16:** repair that logging-pressure fixture faithfully before relying on it; preserve its nonblocking/logger-ownership assertions.
- The first new fixture compile failed because its semaphore endpoint used the wrong field/signature and combined units collided on private logging identifiers. Corrected it to real `backend.count`, `void pbl_sem_give`, production tick conversion and private-name disambiguation; no production guards were weakened.
- `PATH=ARM_TOOLCHAIN:/tmp/pebble-quality-venv/bin:$PATH cmake --build build-quality-finding9-candidate -j 8`: full default ARM/LTO, layout, protected CRC, resource-size and syscall gates pass. FLASH **1,667,136** bytes and RAM **96,640** are unchanged against finding 14; display PSRAM **126,912**, DMA **14,976** and PFS **8,192** remain unchanged. Actual map comparison verifies identical addresses/sizes for all six reserved/retained sections; `.config` is byte-identical. Existing generated-declaration LTO warnings remain.
- Ruff check/format, Clang Format 22 and `git diff --check` pass. Both unrelated planning-file hashes are unchanged. Before/candidate source, reproduction and failure logs, test/build commands/results, default config/map/ELF/dictionary/raw/protected images, review patch/state, verification and SHA-256 manifest are retained under `build-quality-20261121/ipc-init-finding15/`.

Finding 15 is implemented and host-verified, **uncommitted and unflashed**, for
review. Host tests do not qualify CP firmware, physical startup or charging.
The previously installed default finding-13/14 image remains on the watch.
Next ordered source item is **finding 16, actual-thread logging recursion**,
including the newly identified logging-pressure harness repair. Prior battery,
panel and transport bench limitations remain open.


### Finding 15 committed; finding 16 native-thread logging recursion — 2026-11-22

At the operator's request, committed finding 15 as **2c16ed3c2**
(`t5ai: separate flash and ADC initialization`), staging only its eight reviewed
files; DCO/AI trailers and gitlint pass. No flash accompanied that commit.

Reinspection confirms the stock guard indexes by `PebbleTask`; every
unregistered native thread maps to `PebbleTask_Unknown`. A real second native
thread therefore returns NULL before attempting the logging mutex when a first
unknown thread is logging. The new fixture compiles the complete committed
stock applib and SoC adapter units, then reproduces this as an executed
assertion failure: the second unknown thread finishes without waiting for the
mutex. No scheduling/ABI bug is inferred merely from the original audit.

Ordinary system work queues are already registered as `SysWorkq` and
`SysWorkqHigh` in `pebble/main.c`; battery service work uses those queues.
The HCI ACK worker and optional diagnostic workers have independent native
lifetimes and can log. Reusing an occupied PebbleTask identity would obscure
actual task ownership. Moving blocking HCI fault reporting onto a shared work
queue would introduce coupling without fixing other native contexts. Keep
those independent contexts and track the actual current thread.

Extended the existing **SoC-local** `mailbox_logging.c` boundary to own kernel
log-state acquire/release and logging readiness. It reserves existing native
TLS slot **0** for the entered marker (slot 1 remains available; repository
inspection found no existing native TLS-slot users). A compile-time assertion
requires the reserved slot. `pbl_thread_create()` already clears thread objects,
so a fresh thread does not inherit the marker. The marker is claimed before
mutex acquisition and cleared on failure or after release: same-thread
recursion during waiting/unlocking is rejected while distinct threads wait
normally. Identity uses `pbl_kernel_is_started()`, because a scheduler-locked
thread still has its own identity even though `pbl_kernel_is_running()` is false.

Preserved the stock one-second mutex timeout, early boot logging, ISR/critical/
scheduler-locked nonblocking admission, pre-applib thread-mode rejection, and
single formatting-buffer ownership. IRQ locking protects buffer admission and
per-thread marker updates. Release remembers whether this acquisition actually
took the mutex, avoiding an unmatched unlock if readiness/registration changes
while an early log is in progress. The mailbox owner still returns NULL before
log-state/PULSE locks or recursion-state mutation.

CMake renames the stock acquire, release and init definitions only for T5AI;
all callers resolve to the SoC boundary through LTO. The init wrapper retains
the stock initialization of all other applib services. Stock logging source,
frontend/App/Worker dispatch, task enum and native thread layout remain
byte-identical to HEAD. No common production edit, extra thread slot, dynamic
allocation, retained field or ABI/layout change is required. LTO removes the
unused stock kernel buffer guard/acquire/release code.

Validation:

- Added eight methods compiling the **complete production SoC adapter and stock applib translation units**, plus `kernel/tick.c`, real headers, board Kconfig, `LogState`, native thread/mutex types and TLS accessors. Kernel/IRQ/CP-owner/service endpoints are modeled; two actual pthreads contend through a modeled native recursive mutex with the real one-second timeout. No new function extraction or copied wire/type layouts are used in this fixture.
- ASan/UBSan checks cover distinct unknown-thread serialization and formatting-buffer integrity, same-thread recursion, real contention timeout followed by successful retry, recursive logging during lock failure and unlock, unused TLS-slot preservation, pre-scheduler logging without querying a current thread, the pre-applib window, ISR/critical/scheduler-locked nonblocking paths, cross-thread critical rejection, mailbox-owner early exclusion, and release after readiness changes.
- Repaired the preexisting logging-pressure fixture dependency identified in finding 15: supply a counting endpoint for `t5ai_hci_notify_work()` and require **66 notifications** (routing and fault paths each notify for all 33 frames). Its buffer-provider stub follows the adapter's new private helper name. The original blocking-logger, 32 physical ACKs/drains/supervision, mailbox exclusion, first-descriptor preservation and saturation assertions remain intact. This older fixture still extracts helpers; the new logging fixture and existing complete HCI work/ACL suites independently cover actual units. No production guards were weakened.
- `PATH=/tmp/pebble-quality-venv/bin:$PATH ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest tools.tests.test_t5ai_logging_threads tools.tests.test_t5ai_mailbox_failures tools.tests.test_t5ai_hci_work tools.tests.test_t5ai_hci_sleep tools.tests.test_t5ai_hci_acl tools.tests.test_t5ai_hci_pressure tools.tests.test_t5ai_ipc_init tools.tests.test_t5ai_saradc tools.tests.test_t5ai_battery tools.tests.test_t5ai_battery_events tools.tests.test_t5ai_build_integration tools.tests.test_t5ai_config -v`: **79 methods pass**, including the formerly failing logging-pressure method. The committed baseline's distinct-thread assertion failure is retained separately.
- `PATH=ARM_TOOLCHAIN:/tmp/pebble-quality-venv/bin:$PATH cmake --build build-quality-finding9-candidate -j 8`: full default ARM/LTO, image-layout, protected CRC, resource-size and syscall gates pass. FLASH **1,667,232** bytes (**+96** against finding 15); RAM **96,608** (**-32**). Display PSRAM **126,912**, DMA **14,976**, PFS **8,192**, all six actual retained/reserved section addresses/sizes and `.config` remain unchanged. Existing generated-declaration LTO warnings remain.
- Actual Ninja compile definitions contain all three renamed stock boundaries. Linked-symbol inspection confirms the old stock acquire/release functions and shared task-indexed entered array are absent. Shared logging/task/frontend/thread files and both unrelated planning-file hashes remain unchanged.
- Ruff check/format, Clang Format 22, `git diff --check` and finding-15 commit gitlint pass. Before/source snapshots, baseline failure, thread/pressure/79-method/build logs, actual compile boundary and linked symbols, candidate config/map/ELF/dictionary/raw/protected images, review patch/state, verification and SHA-256 manifest are retained under `build-quality-20261121/logging-thread-finding16/`.

Finding 16 is implemented and host-verified, **uncommitted and unflashed** for
review. No hardware capture, reset, flash, provisioning or formatting was
performed. Host tests do not qualify physical UART/Bluetooth/storage contention
or fault-handler behavior on the watch. The previously installed default
finding-13/14 image and its recorded battery/panel/transport limitations remain
current. Next ordered source item is **finding 17, precise recovery/install
capabilities and board-owned pairing buttons**.


### Findings 15/16 installed and bounded bench — 2026-11-22, 18:09–18:13 UTC

At the operator's request, flashed the exact sealed default AP candidate:
**1a26e7779b0e0bd922d3048b25b5a1f5cf2e9ecc**,
`v4.41.2-1664-g2c16ed3c2-dirty`. Protected SHA-256
`ba584345a97edf1c7bb12b904933d5e13777d55fdffa94bafae5bb3307a03f91`;
raw SHA-256
`f4bb99207b1e5e7fa6c7360ad1e5c97c50f41e70f26b823cfd8bf6ed3cc253a7`.
This is now the installed image, including committed finding 15 and uncommitted
finding 16. The preceding unflashed statements are historical source checkpoints.
All 32 sealed source/build evidence files and eight live review files matched
before flash; the linked ELF build ID matches UART.

`PYTHONPATH=tools/libs/pbl-cli /tmp/pebble-quality-venv/bin/python -m pbl lab --json`
reports an empty inventory. The test venv has no `pbl` executable; invoking the
current repository module resolves that tooling issue. Actual host enumeration
confirms CH342 if00/if02 (ttyACM0/1) present and free. if02 capture receives 224
bytes before flash; raw buffered old-image data is preserved and excluded from
new-image analysis at the new boot banner/build ID. The phone is online via ADB.

Command: `/tmp/pebble-quality-venv/bin/python -u tools/flash_unhygienix_firmware.py --raw CASE/pebbleos.bin --protected CASE/pebbleos.t5ai-crc.bin --tyutool /tmp/pebble-quality-tyutool --port /dev/serial/by-id/usb-1a86_USB_Dual_Serial_5C75025844-if00 --flash`.
The guarded AP-only write completes in **52.1 seconds**, physical range
**0x132000..0x2e27aa exclusive**. No CP/resource/PFS/calibration/bond programming
or clearing, formatting or provisioning was performed. Battery remains
**disconnected**, as previously requested; power was kept attached.

Results:

- New-image UART reaches Launcher at **18:09:51.803**, with startup RTC anchor 18:09:40.000. Existing SD PFS is `new=0`, resources validate and 30 apps are found. All **15 graphics output checks pass**. At 18:10:52.470, the AP-only sleep report records **32 returns, fault=0**; this is a report checkpoint, not an exact final sleep count or power/CP-sleep qualification.
- Operator confirms **all five dim → wake → swipe cycles pass**, with menus responsive and normal display recovery. UART capture retains **5,613 bytes over 268.7 seconds**, including preflash data and the commanded boot. Exactly one new-image AP boot banner appears; no subsequent AP boot/assertion/hard fault is observed in this bounded capture.
- Initial companion readiness says `no watch connected`. Restarting only `coredevices.coreapp` restores the existing bonded/encrypted session without another watch reset or clearing bonds. Do not claim automatic-reconnect qualification or attribute the initial failure solely to the phone.
- The captured phone check passes **three pongs (186–297 ms)**, expected version/git, current app query and complete **260×260 / 67,600-byte screenshot** in **8.57 seconds**. Screenshot SHA-256 `b6d5c82dbf2981eab6d81c1423280443ffbce119c80156457f5136f9e3b3eadc` matches the previous Ping image. A later connection-only check passes **three pongs (216–369 ms)** and version without restarting the watch or companion.
- Preserve warnings: dropped app event type 27 and two animation-2 errors also exist in the earlier default control; this capture additionally records pairs of animation-7/8/10 errors during interaction. Their cause is not established. At **18:12:43.689** a connection-parameter request times out/re-requests; parameters update at **18:12:44.270**, with no captured disconnect/AP reset. Existing CP temperature/boot diagnostics are retained. This is not a clean transport/fault qualification, and warnings were not suppressed.

Evidence: `build-quality-20261121/logging-thread-finding16/device-20261122-180820/`
contains exact images/config/ELF/map/dictionary, flash and raw/timestamped/decoded
UART, first readiness result, phone logcat/results/screenshot, operator response,
verification and a separate hardware SHA-256 manifest. Captures are stopped,
both UARTs are released, and ADB forwarding is removed; the companion and
Bluetooth are left running. The original source/build seal remains immutable;
the case's `post-bench-source/` and `review-state.json` record the current eight
review files after this journal update.

Finding 16 passes the targeted normal-use bench and the 79 host checks already
recorded. Forced native logging contention, logger/PULSE fault pressure and
independent client error/retry remain host-tested, not on-watch fault-injected.
No new source fix or commit was made in this bench. Paused for review/commit
before finding 17. Earlier actual-cell charging, idle-energy, optional panel
reprepare/fault timing and broader transport/soak acceptance remain open.


### Finding 16 committed; finding 17 explicit recovery/board policy — 2026-11-22

At the operator's request, committed the bench-verified finding-16 increment as
**6d76a70ae** (`t5ai: guard kernel logging by native thread identity`). Only its
eight reviewed files were staged after checking the 43-file hardware evidence
seal and post-bench source snapshot; DCO/AI trailers and gitlint pass. The
installed build remains the finding-15/16 image **1a26e7779b0e0bd922d3048b25b5a1f5cf2e9ecc**.
Its recorded phone-restart, animation and connection-parameter warnings remain.

Reinspection confirms both finding-17 policy checks: factory reset excludes
recovery selection/invalidation by `SOC_T5AI`, and shared pairing UI assigns
Unhygienix buttons by board name. These are policy-coupling cleanup findings,
not proof of a current runtime regression. The complete committed factory-reset
unit retains the expected operation order in 12 baseline profiles.

Semantics matter: existing `RECOVERY_UPDATE_SUPPORTED` admits writing/installing
a recovery image. A platform can have a bootable, read-only recovery image while
rejecting recovery updates. Added hidden **`RECOVERY_BOOT_SUPPORTED`**, default
**y** for stock platforms and **n** in T5AI's SoC defaults. Factory reset uses
this precise capability to select PRF before the filesystem wipe, and still
invalidates slots 0/1 only with PBLBOOT. Neither update support nor SoC identity
controls that decision. The recovery-firmware erase path, non-PFS cleanup,
worker wait, shutdown fallback and final reboot/reason order are unchanged.
T5AI cannot enable the hidden capability by a user override; existing firmware,
recovery-image and system-resource update capabilities remain disabled.

Moved Select/Back pairing assignments into `boards/unhygienix/unhygienix.h`.
Shared UI includes the existing board-header boundary and provides independent
Up/Down fallbacks. Button enum values, click handlers, action-bar artwork,
Bluetooth identity and all other board settings remain unchanged.

**Why shared edits are required here:** the existing shared factory-reset
consumer must test a generic capability, and shared UI must accept board-supplied
controls. A hidden shared capability declaration plus the two narrow consumer
hooks replace policy checks already present in common code. A board/SoC-local
function replacement would duplicate the stock reset/UI implementation or need
compile-time interception. Platform defaults and button values remain local;
no shared implementation is copied and no broad reset/pairing rewrite is made.

Validation:

- Before edits: `ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest tools.tests.test_update_capabilities -v`: **5 methods pass**. A separate complete-unit baseline comparison passes **12 profiles**, preserving stock normal reset ordering (with/without PBLBOOT and shutdown fallback), T5AI's recovery exclusion, and the existing recovery-build erase path.
- Four new `tools.tests.test_platform_policy` methods use real board Kconfig and production headers/constants. The complete `factory_reset.c` translation unit is compiled without extraction under ASan/UBSan. **32 combinations** cover recovery boot/update capabilities, PBLBOOT, normal/recovery builds and shutdown. Two additional non-T5AI cases prove the guard obeys the capability instead of SoC identity. Every case checks the exact ordered cleanup, two 3 ms worker-wait sleeps, recovery selection/invalidation before the wipe when permitted, format versus partition erase, shutdown fallback, reset reason and reboot. All destructive endpoints are host models; no hardware format/reset command is executed.
- Recovery boot defaults are checked for Unhygienix, Obelix, Asterix and Getafix DVT/DVT2 in normal/PRF configurations, including rejection of a T5AI user override. The complete pairing translation unit is preprocessed for the same **ten board/variant profiles**; compiled assertions use its resulting macros and the real `ButtonId` enum to verify Select/Back or stock Up/Down and distinct buttons. Unused generated resource/font headers are empty only for this preprocessing check; no resource IDs, UI layouts or rendering are modeled. The default ARM build compiles the complete UI with actual generated resources. No on-watch pairing/decline or stock-board boot claim follows from preprocessing.
- Initial fixture runs exposed a mock prototype mismatch, nonexistent Getafix defconfig-overlay assumption, the stock partial-initializer warning and missing generated/UI include paths. Corrected the fixture to real signatures, actual available config inputs and upstream uPNG headers. `-Wno-missing-field-initializers` matches the production build's existing treatment of stock initializers. No production initializer, guard or validity assertion was weakened. Failed and passing setup logs remain separate.
- Final command: `PATH=/tmp/pebble-quality-venv/bin:$PATH ASAN_OPTIONS=detect_leaks=0 /tmp/pebble-quality-venv/bin/python -m unittest tools.tests.test_platform_policy tools.tests.test_update_capabilities tools.tests.test_unhygienix_boards tools.tests.test_getafix_boards tools.tests.test_t5ai_buttons tools.tests.test_t5ai_config tools.tests.test_t5ai_reset tools.tests.test_unhygienix_image tools.tests.test_t5ai_build_integration -v`: **47 methods pass**. Existing update-admission/cancellation/side-effect guards pass unchanged.
- `PATH=ARM_TOOLCHAIN:/tmp/pebble-quality-venv/bin:$PATH cmake --build build-quality-finding9-candidate -j 8`: full default ARM/LTO, layout, protected CRC, resource-size and syscall gates pass. FLASH **1,667,232** and RAM **96,608** bytes are unchanged against finding 16; display PSRAM **126,912**, DMA **14,976** and PFS **8,192** remain unchanged. Actual map comparison confirms all six reserved/retained section addresses/sizes unchanged. `.config` is byte-identical; the new hidden false capability is omitted from it and undefined in `autoconf.h`, as are the three disabled update capabilities. Existing generated-declaration LTO warnings remain.
- Ruff check/format, Clang Format 22, whitespace checks and finding-16 commit gitlint pass. Both unrelated planning-file hashes remain unchanged. Before/source snapshots, baseline/equivalence/setup/final test/build logs, candidate config/map/ELF/dictionary/raw/protected images, verification, review patch/state and SHA-256 seal are under `build-quality-20261121/platform-policy-finding17/`.

Finding 17 is implemented and host-verified, **uncommitted and unflashed**, for
review. No hardware flash, reset, provisioning, formatting, power or bond action
was performed. The previous installed image and bench limitations remain current.
Next ordered source item is **finding 18, retained-storage interfaces**; preserve
existing word layout/checksum, transactional clock behavior and old-image
compatibility when reassessing that finding.
