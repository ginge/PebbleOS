> Historical CP clock investigation. The current production profile keeps CP
> and shared clocks awake; see [AP-only retained idle](ap-only-sleep.md) and
> [current status](current-status.md). Statements below describe the investigation.

# Active clocks and coordinated sleep — 31 October 2026

The intended Unhygienix policy retains the normal 240 MHz active vote. Clock
reduction belongs inside a complete, coordinated suspend/resume transition.
The failed fixed 120 MHz startup selector and bounded frequency probe have been
removed from source and Kconfig. The active startup vote is always 240 MHz.
Read-only clock reporting and tick calibration remain. No retained-sleep path is
enabled; the application sleep veto and normal SysTick remain in place.

See the latest [coordinated-sleep implementation and blockers](coordinated-sleep.md).
The default-off candidate now fits the authorized expanded AP subdivision.
Bounded retained trials failed: the latest pilot froze/disconnected before a
completed CP low-voltage residency could be established. Known normal firmware
is restored. Independent lost-wake recovery, native PSRAM/AHBP retention and
radio/clock/timer qualification remain gates; see the latest checkpoint above.

1 November: the CP-ready fence now covers flash/peripheral restore, and AP uses
full PRIMASK inside SRAM. Independent watchdog reset timing, attribution and
reset-surviving stop/evidence remain unqualified. Guard probing now requires the
CP write lease plus an explicit expiring UTC slot; zero disables arming. V5 is
built but unflashed. Keep the known pair and all sleep experiments default off;
see the latest [guard checkpoint](coordinated-sleep.md).

## PR2 reference

`soc/sf32lb/sf32lb52x/idle.c` implements three distinct sleep levels. Its deepest
retained path runs from RAM, saves/masks interrupts, powers down flash, switches
HCPU from DLL1 to HRC48, saves the DLL1 frequency and disables DLL1. On wake it
restores power/pads, waits for HXT48, enables DLL1 at its saved frequency, selects
DLL1, releases flash deep power-down and restores interrupt enables. The caller
arms LPTIM, stops SysTick, compensates elapsed low-power timer time, and restarts
SysTick. IPC idle, sleep blockers, a minimum idle interval and the final kernel
idle confirmation guard entry. A persistent 120 MHz vote is not this mechanism.

## Corresponding pinned T5AI mechanism

In `third_party/hal_tuya/TuyaOpen-T5AI/t5_os/cp/middleware/soc/bk7258/hal/sys_pm_hal.c`,
`sys_hal_set_cp_sleep_vote_and_check_subcores_enter_wfi()` checks the CP low-voltage
vote and both AP WFI states. The retained low-voltage sequence saves the shared
core and flash sources/dividers, switches to 26 MHz, saves/disables high-frequency
clocks including DPLL, and enters sleep. Wake restores voltage, high-frequency
clocks and power domains, waits using AON time for DPLL stability, restores the
saved core/flash clocks, then restores interrupt/tick state. These are source
findings; this native Pebble kernel has not qualified that sequence.

The AP HAL's `sys_hal_enter_normal_sleep()` under `CONFIG_PM_LV_SUBCORES_ON` waits
for the CP sleep vote, rejects pending interrupts or active DMA, preserves IRQ
routes while allowing mailbox wake, publishes its AP WFI state, and clears it on
wake. Its FreeRTOS callbacks, sibling-AP yields and tick handling cannot be called
unchanged from the native kernel. The CP owns the shared clock/voltage transition;
AP must not directly disable a PLL while Bluetooth or the other AP uses it.
HRC48/DLL1 are SiFli names; T5AI uses its own 26 MHz source and DPLL sequence.
PR2's flash command must likewise not be copied onto memory shared with CP.

The `vote_cpu1_frequency` protocol name identifies the vote owner, not the only
core affected. CP takes the maximum module vote and changes shared core clocks
and voltage. Archived hardware snapshots in the removed diagnostic probe confirm both CP0 and AP
at 240 MHz with digital-voltage selector D, then both at 120 MHz with selector C.
Those selector values are register observations, not measured rail voltages.
A frequency ACK follows application of the vote; wake-alarm ACK ordering differs.

## Implementation order

1. Implement a board-owned CP alarm operation with applied status, owner and
   generation, safe payload lifetime and cancellation. The pinned legacy alarm
   reply precedes installation and hides registration errors. Preserve all other
   alarm users and radio deadlines. See [wake contract](wake-alarm-contract.md).
2. Qualify AP wake delivery and exactly-once AON elapsed-time accounting, including
   pending wake, early interrupt, wrap, failed arm and stale generation. Keep the
   application veto until physical wake is reliable.
3. Add balanced native sleep blockers for mailbox, storage, shared memory, DMA,
   UART and other active peripherals. Integrate CP and sibling-AP readiness,
   atomically recheck idle, and abort safely on any changed condition.
4. Add retained entry/resume using T5AI's CP-owned clock/power ordering. Preserve
   the active vote, restore saved clocks and tick calibration before normal work,
   and retain heartbeat, radio supervision and shared memory/cache ownership.
   Existing display transfer activity remains a blocker; leave display settings
   and display power policy fixed during non-display qualification.
5. Validate deadlines, PFS durability, reconnects, actual cold power and overnight
   connected soak. Measure energy per completed workload and idle input power.
   Overnight remains deferred by the operator; no savings are claimed here.

The operator observes dropouts only after the 120 MHz trials. Warm restoration
of the AP leaves CP powered, so later entry-AP dropouts cannot establish that the
120 MHz transition is unrelated. A full cold control and a single-boot
240→120→240 diagnostic are recorded under
`artifacts/bt-frequency-20261031` in the reference workspace. Short clean windows
cannot disprove intermittent failures or establish production radio qualification.
