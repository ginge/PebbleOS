# Unhygienix battery subsystem review — 2026-10-18

## Decision

The working tree now uses stock voltage-service demand with a nonblocking,
SoC-local CP ADC backend. Its private sampling worker and 2,048-byte stack are
removed. The mailbox owner advances ADC transactions; KernelBG and NewTimer do
not wait for CP conversion or mailbox replies. The current implementation and
qualification limits are recorded below. The original findings and staged plan
are retained as the review baseline, not outstanding instructions to repeat work.

## Reviewed inputs

Production HEAD: `08a8212e8c043288c860bcb9b4090269294bd39c` (clean at review start).
Local `origin/main`: `f40a377469d5ed0083a8b7b0be78b4e51954623e`; no fetch performed.
Vendor checkout revisions: TuyaOpen `e82840dc9b629cb77fa922fb04e3f820dc2e78b4`,
TuyaOpen-T5AI `13379b63e07e78770fb4d0bffe36db2754658132`. Vendor source explains
protocol intent; the pinned CP binary remains the actual runtime authority.

- [T5AI battery driver](../../../drivers/battery/t5ai.c),
  [driver contract](../../../include/pbl/drivers/battery.h),
  [common wrappers](../../../drivers/battery/battery_common.c).
- [SARADC client in flash IPC](../../../drivers/flash/t5ai_flash_ipc.c),
  [MB-IPC](../../../drivers/t5ai/mb_ipc.c),
  [physical mailbox](../../../drivers/t5ai/mailbox_channel.c).
- [Voltage state service](../../../fw/services/battery/voltage/battery_state.c),
  [voltage curves](../../../fw/services/battery/voltage/battery_curve.c),
  [monitor](../../../fw/services/battery/battery_monitor.c),
  [Nordic fuel-gauge service](../../../fw/services/battery/nrf_fuel_gauge/battery_state.c).
- [nPM1300 driver](../../../drivers/pmic/npm1300.c), Asterix/Obelix/Getafix
  board declarations and defconfigs, service Kconfig, QEMU battery implementation.
- TuyaOpen `boards/T5AI/WAVESHARE_T5AI_TOUCH_AMOLED_1_75/board_com_api.c` and
  `src/peripherals/power/tdd_power/src/tdd_power_soc.c`;
  TuyaOpen-T5AI `tuyaos/tuyaos_adapter/src/driver/tkl_adc.c` and AP/CP
  `middleware/driver/saradc/saradc_{client,server}.c`.
- [Latest hardware report](/home/baz/Documents/ChatGPT/pebble/artifacts/cst92xx-kernelbg-20261018/flash/report.txt)
  and its `post-boot.log`: battery ADC failure `-12` at firmware 12:23:46.523.
  The operator confirms on 2026-10-18 that the board remains USB-powered with
  no battery fitted. Charger-node voltage does not validate a cell.

## Comparison with other devices

| Concern | Asterix (nRF52), Obelix/Getafix (SiFli) | Current T5AI | Intended alignment |
| --- | --- | --- | --- |
| Hardware driver | Shared nPM1300 device, board configuration | ADC/GPIO singleton with board constants inside driver | Board-owned channel, divider, calibration and charge GPIO; local hardware driver |
| Sampling owner | Service requests measurements through battery API | Driver thread samples independently; API reads cache | Service-driven acquisition if whole-operation latency permits |
| Scheduling | Timer defers service work to KernelBG; fuel gauge coalesces pending updates | Separate 30 s startup delay, 5 s retries, 60 s worker loop plus service's own 60 s loop | Existing service cadence; narrowly scoped startup/retry handling |
| Plug/charge state | Independent VBUS and charger status, debounced connection events | Both boolean APIs read CHRG P30; no connection event | Stock event path with explicit single-signal hardware limitation |
| State of charge | nRF fuel gauge with measured voltage/current/temperature and battery model | Stock voltage filter and Silk voltage curves | Retain voltage service; qualify this cell's curve independently |
| Failure reporting | Voltage getter returns zero; constants getter can fail | Cached/default voltage returned as success indefinitely | Track valid/fresh/failed acquisition explicitly; define safe service behavior |

The Nordic library is selected by PMIC capability, not by the CPU being Nordic.
Copying its fuel-gauge implementation would require measurements and a battery
model this hardware does not supply. Both SoC families demonstrate the same
service ownership and event boundary; their PMIC register details do not transfer.
The stock nPM1300 voltage conversion polls for up to 100 ms (5 ms sleeps), in
addition to bus work. This is a comparison point, not a measured T5AI budget.

## Findings at the review baseline

1. **The ADC path is not currently qualified.** The latest installed image reports
   `E_AGAIN` (`-12`). The battery worker aborts on that or `E_INVALID_OPERATION`.
   There is no restart and `s_started` remains true. `battery_get_millivolts()`
   continues returning its last value, initially 3,700 mV; constants returns
   success, with zero current and temperature. The trace does not identify the
   failing ADC phase or prove any successful measurement. UI responsiveness is
   not battery validation. Do not attribute the separate KernelBG watchdog
   finding to this ADC timeout without further evidence.

2. **Per-call timeouts do not bound a complete sample.** A successful acquisition
   uses 12 command transactions: GPIO init, acquire, init, config, bypass, start,
   raw read, read-done, stop, deinit, release, GPIO deinit. Each transaction has
   separate 2 s logical waits, up to five receive attempts for stale completions,
   and 5 s physical operations underneath. Connection has a separate 500 ms
   logical wait; mutex waits are unbounded. A physical timeout may reset both
   CPUs. The 2 s constant is not a sample deadline. Transactions also temporarily
   raise the calling task's priority. Moving this unchanged into KernelBG could
   block all following callbacks, including touch. Feeding the watchdog would
   not solve that blocking.

3. **Transport status and ownership need a focused correctness review.** After
   `SARADC_READ_RAW`, the failure branch replaces its actual error with
   `E_ERROR`. If a timeout quarantined the socket, the caller learns only a
   generic failure and retries before observing permanent unavailability.
   CRC/size rejection skips READ_RAW_DONE and goes directly to STOP, whereas
   the CP server waits up to 50 ticks for READ_RAW_DONE and consumes the next
   message in that phase. This can misinterpret cleanup as the read-done
   handshake. Reproduce and fix those paths against the pinned ABI, preserving
   quarantine of any storage still visible to CP. Do not shorten physical
   timeouts or reconnect/reuse ambiguous descriptors merely to meet a budget.

4. **CHRG is not VBUS detection.** Both T5AI boolean APIs return active-low P30.
   The vendor board has no STDBY line; its implementation also cannot observe
   full versus disconnected from CHRG alone. A charging-to-full transition can
   therefore appear as unplugging and prevent the stock full-charge transition.
   This is a hardware observability limit, not something a copied PMIC state
   machine can fix. No independent usable VBUS signal was established by this
   review; verify the schematic/available CP interface before claiming one.
   The reported `BatteryChargeStatusCC` also asserts a charging phase the GPIO
   cannot distinguish. Charger enable/fast-charge setters are no-ops.

5. **Connection notifications are missing.** T5AI configures P30 as input but
   never emits PEBBLE_BATTERY_CONNECTION_EVENT. Stock PMIC defers its IRQ,
   debounces, then emits that event; the service schedules an update after 1 s.
   T5AI currently notices changes only at the service's polling cadence and
   misses event-driven consumers such as stationary state. Existing T5AI GPIO
   interrupt support already provides emulated both-edge triggering. Reuse it,
   debounce and coalesce notifications, reconcile the initial level, and avoid
   claiming that a CHRG edge proves a USB insertion/removal.

6. **Calibration constants have provenance, but sampling equivalence is unproven.**
   ADC channel 15 / P13, divider 2510/510 and low/span 2716/2854 match the vendor
   board's documented meter calibration. Keep them initially, as board data.
   Pebble takes the peak of one 16-sample continuous acquisition; the reference
   takes the maximum of 16 individual calls, each acquiring/starting/stopping
   the ADC. The reference explains a high-impedance divider that sags during
   sampling. These waveforms are not automatically equivalent. Compare raw
   distributions, settling and meter readings before retaining or changing the
   sampling pattern. Pebble also truncates tap millivolts before applying the
   divider, unlike the reference float conversion. Arithmetic cleanup should
   follow tests, not quietly alter calibration. Keep filtering in the stock
   voltage service rather than copy the vendor's additional EMA.

7. **The percentage model is inherited, not characterized.** Unhygienix selects
   the Silk curves (discharge zero at 3,300 mV, full at 4,230 mV). The vendor
   table spans 2,800–4,200 mV and gives materially different percentages; neither
   table is established for a fitted cell by this review. Preserve existing
   thresholds during transport work. Characterize the actual cell separately;
   do not import vendor low-voltage cutoffs as part of thread consolidation.
   BoardConfigPower only sets a 5% reserve and leaves battery_capacity_hours
   zero, so hours estimates are not calibrated either. Existing 12%/8% warning
   settings avoid the default time-based warning thresholds.

8. **Configuration and tests encode early-port choices.** Battery CMake selects
   t5ai.c using SOC_T5AI rather than BATTERY_T5AI. Align source selection with
   the declared driver option. Existing tests insist on a private worker and
   cached getter or extract initialization fragments; they do not exercise
   the ADC sequence, cache freshness or hardware behavior. Replace these
   architecture assertions with full-driver behavior tests when changing it.
   The existing native battery-monitor suite tests the shared voltage service
   with a fake driver and remains useful regression coverage.

## Original implementation sequence

1. **Establish and repair ADC acquisition first.** Add bounded, phase-specific
   diagnostics for connection, each command, raw-read/read-done and cleanup.
   Include elapsed time and terminal status; avoid periodic informational spam.
   Separate SARADC implementation from flash into a SoC-local client while
   retaining the shared MB-IPC transport, ABI, notification registration order
   and CP-owned conversion. Test first, then repair error propagation and the
   CRC/size/cleanup handshake. Preserve descriptor lifetimes on timeout. Capture
   the first failing phase on the current board and obtain repeatable successful
   reads before choosing a different scheduling owner.

2. **Restore a deliberate driver contract.** Put board wiring/calibration in a
   small board-owned configuration using existing native GPIO/device patterns.
   Check GPIO initialization errors. Define how unavailable, never-sampled and
   stale data are represented, and how startup avoids false critical-battery
   transitions. The stock voltage service does not reject zero readings, so
   mechanically returning zero on failure is insufficient. Prefer the smallest
   local solution; if the public API cannot communicate invalid data safely,
   document the necessary shared change separately before making it. Keep the
   voltage-based service and existing UI, monitor and warning behavior.

3. **Integrate charger events within observable hardware limits.** Establish
   whether an independent USB-present signal exists. If it does, model it
   independently from CHRG. Otherwise document the restricted semantics and
   test them explicitly; do not invent a full/USB status. Use the native GPIO
   IRQ, deferred debounce and existing connection event path where applicable.
   Timers/ISRs only schedule work. Confirm initialization order before enabling
   IRQ delivery. Keep charger-control capability explicit.

4. **Consolidate scheduling only after the latency result.** Preferred design:
   the voltage service's existing KernelBG callback calls the battery driver
   for a measurement, with a short whole-operation budget and no private
   sampling thread. Retain only justified local startup/rate-limit/cache
   behavior, not a second periodic sampling loop. Measure full sample and
   cleanup wall time under flash/Bluetooth/touch load, including CP's 50-tick
   read-done window. If that cannot meet the background-work budget, use a
   narrowly scoped asynchronous SARADC transaction state machine with bounded
   callbacks and native completion signaling; evaluate its complexity and RAM
   before selecting it. Never run the current blocking client on its mailbox
   completion-owner thread: it depends on that owner to make progress. Retain
   the current worker until a safe replacement is demonstrated.

5. **Validate behavior and memory, then flash a concrete candidate.** Full-driver
   host tests must cover startup, valid/invalid raw data, stale/never-valid data,
   recoverable failure versus quarantine, timeouts in every command/cleanup,
   CRC/size rejection, late/stale responses and descriptor lifetime. Exercise
   charge IRQ bounce, both edges, initial asserted state, queue rejection and
   duplicate updates. Run the native battery-monitor suite and relevant MB-IPC/
   tooling suites, build/package, and compare static RAM plus stack/heap usage.
   On device, measure sampling and touch/background latency under concurrent
   flash and BLE traffic. Battery calibration, charge termination, true unplug,
   discharge curve and low-power transitions require a fitted cell and meter;
   USB-only tests cannot close those items. Preserve the first failure and
   distinguish this subsystem's errors from the existing watchdog findings.

## Completion criteria

Item 2 closes only when the acquisition path works, charger limitations are
accurately represented, service integration follows the stock boundary, and
startup/failure behavior is safe and tested. Removing a thread alone does not
close it. The 2,048-byte stack is a prospective saving, not recovered RAM yet;
report net object/timer/state-machine costs and measured headroom with the final
candidate. Hardware qualification remains separate from host-test success.

## Sample-validity correction — 2026-10-18

The operator requested correcting misleading cached returns now, with a future
fitted battery in mind. This implementation has no USB-only bypass or fabricated
startup voltage. That initial candidate retained the sampling worker. The asynchronous implementation
below supersedes its scheduling while retaining its validity rules.

- Removed the 3,700 mV seed. Before the first successful acquisition, the voltage
  API returns its documented failure value (zero); constants returns an error.
- Successful ADC data is available for less than the 60-second sampling interval.
  ADC failure, implausible conversion or initialization failure immediately
  invalidates it. A stalled worker cannot expose a sample indefinitely: expiry
  uses monotonic 64-bit uptime. Status, timestamp and voltage are published/read
  together under a short native IRQ lock. Constants output is untouched on error.
- Range failures retry after 5 seconds. Existing terminal timeout quarantine and
  worker-abort behavior remain; this change does not claim to repair the CP ADC.
- The voltage service skips failed reads, preserves its last accepted estimate,
  and emits no fresh percentage/low-power event from those failures. Its first
  real sample seeds the filter and is published immediately; a real low first
  reading still invokes the stock first-run lockout policy. The small shared
  service guard is required because a local driver cannot both return the
  documented failure value and prevent the unguarded service filtering that
  value into its charge estimate. No T5AI conditionals or substitute service
  were added to common code.
- Existing public charge-state APIs have no validity field; before a real reading
  their zero-initialized percentage is not a measured state of charge. A previously
  accepted UI estimate remains historical during acquisition failure. The raw
  measurement APIs now distinguish that from current valid data.

Four full-driver host cases replace the old extracted initialization test and
cover initialization/retry, valid data, errors, permanent unavailability,
implausible raw values, recovery, expiry and uptime beyond 32 bits. Three new
native battery-monitor cases cover unavailable startup, failure/recovery after
a valid estimate and a real low first reading. Validation and the saved candidate
are recorded in
`/home/baz/Documents/ChatGPT/pebble/artifacts/battery-validity-20261018/validation.txt`.
This correction does not close item 2 or claim worker-stack savings. Meter/cell
qualification is still required when a battery is fitted.

## Asynchronous acquisition and worker removal — 2026-10-18

The operator resumed the remaining repair/consolidation work. Implemented:

- [SARADC client](../../../drivers/t5ai/saradc_ipc.c) is separate from flash.
  CP wire ABI, ADC channel, raw configuration, 16-sample peak, conversion timeout,
  notification acknowledgement and calibration values are preserved. The ADC
  output buffer is separate from reusable command storage and remains static.
- [MB-IPC](../../../drivers/t5ai/mb_ipc.c) provides bounded asynchronous control
  exchanges (at most 256 bytes), copying/validating each response before the
  logical acknowledgement. Its existing mailbox owner advances phases and
  includes the next asynchronous deadline in its native wait. It never runs the
  synchronous MB-IPC client inside the completion owner or a timer callback.
  Existing synchronous flash entry points remain in use.
- A whole ADC session has a 5-second deadline; connect has a 500 ms deadline
  within it and the CP raw conversion retains its existing 2-second timeout.
  These are asynchronous deadlines, not time spent waiting on KernelBG. State
  transitions do bounded work and busy physical slots retry after one tick.
  Physical mailbox fault/reset policy remains at the existing 5-second limit.
- READ_RAW errors retain their status. If CP reports successful raw conversion,
  both valid and rejected CRC/size data complete READ_RAW_DONE before STOP.
  Cleanup acts only on acquired GPIO/ADC ownership. API errors preserve failure
  and attempt applicable cleanup; any cleanup failure disables further use.
  Transport corruption, route rejection or deadline expiry quarantines the
  socket and all CP-visible static storage until reset, including after a late
  physical completion. No reconnection/reuse of ambiguous descriptors is added.
- Native physical-slot serialization now lets synchronous flash wait for an
  outstanding asynchronous command. Its mutex prevents another asynchronous
  sender taking the slot before that flash waiter. Both completion kinds wake
  the waiting client. This is needed because flash and ADC share channel 0x11.
- [Battery driver](../../../drivers/battery/t5ai.c) has no thread, stack,
  semaphore, blocking IPC or sleep. The stock voltage service requests data;
  a pending/failed/expired acquisition returns unavailable. Completion publishes
  status/voltage/freshness under a short IRQ lock. The shared seconds callback
  queues a coalesced system-task notification; the stock force-update path then
  consumes the result. The regular timer never starts ADC or runs service work.
  It rescues dropped notifications and handles the initial 30-second readiness
  delay and 5-second recoverable-error retry. It adds no independent periodic
  ADC sampling loop. Routine minute updates remain owned by the stock service.
- [Board-owned battery configuration](../../../fw/board/boards/board_unhygienix.c)
  declares P30 CHRG, the native both-edge IRQ, channel 15/P13, 16 samples,
  divider and calibration. BATTERY_T5AI now controls source selection. Charge
  edges mark a debounce timestamp; after at least 400 ms of stability the
  shared seconds callback queues a system-task connection event (up to roughly
  1.4 seconds after an edge). Idle reconciliation reads only GPIO. A lost edge
  or queue rejection is retried. Initial IRQ failure falls back to that
  reconciliation; no USB presence/full/CC-CV capability is invented beyond the
  existing restricted API. Charger-control setters remain unsupported no-ops.

Host validation: 298 tooling entries pass with two skips, including four complete
battery-driver cases, seven combined production MB-IPC/SARADC cases (with command
failure subcases), physical-slot serialization and existing heartbeat/flash lease
coverage. All 15 native battery-monitor cases pass. No common driver, scheduler,
watchdog policy or shared battery API was changed for this asynchronous step;
the preceding minimal voltage-service failure guard remains necessary.

Static RAM falls from 101,728 to 99,936 B (1,792 B recovered); linker heap grows
from 25,248 to 27,040 B. The native battery thread object/stack are absent from
the ELF. The 256 B difference between removed stack capacity and net static
saving includes replacement transport/buffer/configuration state and alignment;
these totals are not measured peak heap or stack headroom.

Saved candidate, exact build ID, hashes, configuration, source, build/test logs
and image guard result:
`/home/baz/Documents/ChatGPT/pebble/artifacts/battery-consolidation-20261018/validation.txt`.
At this pre-device checkpoint the firmware had not been flashed. Hardware confirmation that the previously observed
ADC timeout is resolved is still required; host protocol tests cannot establish
that. Read-done latency under concurrent flash/BLE/touch traffic, mailbox stack
high-water, cold-cycle and connected watchdog checks remain open. The operator's
board is still USB-powered with no cell fitted. Battery calibration/settling,
termination behavior, the inherited Silk curve and low-power behavior require
future fitted-cell/meter qualification. CHRG cannot prove full-versus-unplugged
without an additional observable power signal.

## Hardware follow-up and final disposition — 2026-10-18

Final build `779f3e5a1bcaeed66d06043ead303ab3c91254ed` was flashed AP-only
on 2026-10-18 and its boot ID verified. The 300.478-second UART capture received
23,438 bytes, with four successful ADC samples (4,011–4,025 mV, 6–7 ms), zero
ADC errors and one AP boot. The operator confirms watchface, phone connection,
taps/swipes, buttons and sleep/wake all work normally. Encrypted bonding and
phone sessions restored; two discovery completions took 5,059 and 5,265 ms.
The trace still contains 21 KernelBG feed delays, 23 watchdog recoveries and
25 channel warnings. These counts are separate record types, not paired stall
counts; this is not clean watchdog qualification. Capture is stopped.

The first candidate reset after synchronous logging from the mailbox owner
waited on that same owner. ADC diagnostics now log from KernelBG instead.
The next image exposed an inherited checksum error: vendor CRC tests bit 7
of an unmasked 32-bit accumulator, while the port tested bit 31. Independent
vendor vectors and CP-model checks now cover that correction. Failed images,
rollback to the previously tested touch build, and each trace remain preserved.

No cell is fitted: these voltages measure the unloaded charger node, not cell
calibration or battery percentage accuracy. Cold power-cycle, sustained watchdog,
read-done timing under load, stack high-water, peak heap and fitted-cell/meter
qualification remain open. Final evidence:
`/home/baz/Documents/ChatGPT/pebble/artifacts/battery-consolidation-20261018/vendor-crc/flash/report.txt`.

Final host validation passes 299 tooling entries with two skips, including eight
production MB-IPC/SARADC cases and independent vendor checksum vectors. All 15
native battery-monitor cases pass. Logging-context guards cover the mailbox-owner
regression. Final static RAM is 99,968 B, linker heap 27,008 B: a net 1,760 B
recovered against the battery-worker baseline, and 6,080 B across all three worker
consolidations. Diagnostics add 32 B relative to the earlier candidate above.
Actual sampled acquisition durations are not a separate measurement of the
READ_RAW_DONE phase or peak resource usage. Watchdog PC/LR samples are resolved
against the exact final ELF in the evidence directory; they do not establish
root cause.
