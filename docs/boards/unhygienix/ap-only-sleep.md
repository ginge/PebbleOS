# AP-only retained idle

Production retained idle keeps CP awake. Bluetooth, heartbeat, the mailbox
owner and the generation-owned wake alarm remain serviced by CP. AP sleeps
between deadlines; active CPU policy remains 240 MHz. Shared DLLs, flash,
PSRAM, voltage and memory retention remain under their active policy.
Coordinated CP low-voltage sleep is deferred; see
[the preserved investigation](coordinated-sleep-history.md).

## Reproducible profile

Unhygienix defconfig enables `CONFIG_T5AI_POWER_SAVINGS=y`. The profile
selects continuous AP-only retained idle, local I2C/SD/UART clock lifetimes and
quiet successful battery logging. It preserves the existing dim display;
AMOLED zero-brightness and panel sleep options remain disabled by default.
Start from a clean configuration rather than an accumulated diagnostic cache.
Set `CONFIG_T5AI_POWER_SAVINGS=n` for a control without these policies.
Default enablement does not complete the qualification gates below.

The board requires the exact calibrated alarm-capable CP97 package recorded in
[current status](current-status.md). Stock Wi-Fi-off CP is a rollback image,
not an alarm server. Preserve calibration, controller identity, resources,
existing PFS and bonds. Verify actual CP readback before AP deployment.

`T5AI_RETAINED_SLEEP_TEST=n` selects continuous operation after startup settling.
Production Kconfig forces `T5AI_RETAINED_SLEEP_WAKE_ONLY=y`; a compile guard
also rejects production retained idle without it. The historical WAKE_ONLY name
means AP deep WFI with CP awake. Qualification controls live separately in
`boards/unhygienix/Kconfig.power-debug` and require explicit test enablement.

## Suspend and wake contract

AP checks peripheral and IPC blockers, owns an alarm generation before
suspending SysTick, and rechecks deadlines with interrupts masked. The SRAM
wake gate restores AP interrupt routes and sleep-control state before resuming
normal execution. Elapsed ticks are accounted exactly once, retained state is
checked, and the alarm generation is retired. Unknown ownership or failed
restoration closes admission. CP APP sleep veto remains set.

Every AP return checks that CP low-voltage, resource, clock and interrupt-mask
counters have not advanced. AP-only operation must never suspend CP resources,
mask CP interrupts, lower shared voltage, shut down shared clocks or enter CP
WFI. Diagnostic controls cannot override the AP-only operation selection.

## Evidence and release gates

The current-source bounded AP-only trial completed 12 returns with no CP
transition, abort, deadline or contract fault. ADC and encrypted protocol
recovery followed. The subsequent combined image reported 32 returns with
zero CP transition deltas; the operator confirmed display wake/update and
Bluetooth recovery. That image also recorded Bluetooth discovery/timeout
failures, preserved under `artifacts/ap-all-savings-20261108/first-failure` in
the reference workspace.

The installed dim revision is `f1f862e8…`. Its boot contract was received, but
its 32-return milestone was not captured. Cleanup builds have not been flashed.
See [current status](current-status.md) for exact identities and proof limits.

Before release:

1. Run host alarm/coordinator, timer, ownership and fault-injection regressions;
   build the board-default profile and explicit-off control. Audit linked SRAM code,
   image layout/CRCs, resources and display configuration.
2. Verify received UART data, exact AP/CP identities and encrypted protocol
   recovery. Preserve first failure before rollback.
3. Qualify sustained AP returns, timers, ADC/sensors, PFS, reconnects, genuine
   cold power and the deferred overnight soak. Resolve observed Bluetooth
   failures before claiming connection stability.
4. Measure matched connected idle and energy per completed workload with dim
   Nadir and the supply fixed. No combined savings figure is established yet.

Full CP sleep findings and failed experiments remain historical evidence;
this production profile does not enable them.
