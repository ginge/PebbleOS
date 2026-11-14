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

QMI quiet-period WOM is a separate board-default option,
`CONFIG_T5AI_QMI8658_WOM=y`; disabling the umbrella power profile does not disable
WOM. Use `CONFIG_T5AI_QMI8658_WOM=n` explicitly for a matched continuous-sensor
control. Preserve requested sample clients and the normal software shake policy.

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

## Final recovery checkpoint — 14 November

The first normal `faae34b9` installation below passed without an extra reset.
The subsequent power-comparison restoration required one commanded reset after
AP=0x4f, then failed phone reconnection despite 32 clean AP returns. The operator
USB cycle restored an encrypted bonded session and 32 clean AP returns. Current
normal image is unchanged. This is recovery, not clean first-startup release
qualification. See [latest failure and limits](qspi-dma-diagnostics.md).

## Latest bounded validation — 14 November

Normal `faae34b9` is restored after the separate default-off mixed DMA test.
Its first post-programming boot reports AP/CP reset sources 0/0, contract 0x78f,
32 AP returns with all CP deltas zero, encrypted bonded phone recovery and
operator-confirmed normal display/dim, wake and inputs. No extra reset was used.
The qualification image's AP=0x4f / sleep-reason-4 rejection is preserved
separately; do not count that run as retained-sleep success. See
[mixed-I/O method and limitations](qspi-dma-diagnostics.md).

## Evidence and release gates

The 8 November bounded AP-only trial completed 12 returns with no CP
transition, abort, deadline or contract fault. ADC and encrypted protocol
recovery followed. The subsequent combined image reported 32 returns with
zero CP transition deltas; the operator confirmed display wake/update and
Bluetooth recovery. That image also recorded Bluetooth discovery/timeout
failures, preserved under `artifacts/ap-all-savings-20261108/first-failure` in
the reference workspace.

The preceding installed normal AP was
`b40a6cb1070c5440f74c1c758b2c0eff8a38290a`. It preserves the retained-sleep
configuration and linked SRAM invariants while adding the independent
[board standby rejection and panel command cache](standby-display-review.md).
After the final reinstallation's AP=0x4f admission failure, one commanded
same-image reset received reset sources 0/0, contract 0x78f, SD startup and
32 AP returns with all CP transition/clock/mask/resource/fault deltas zero.
The 8,277-byte bounded capture is stopped. CP97 full readback/CRC matched earlier
in the session and no CP write followed. No guard was weakened.

The first installation of this same AP received encrypted bonded phone recovery;
the operator confirmed normal display/dim, first motion, touch and buttons after
a long splash/apparent automatic restart. UART did not corroborate a second boot.
No new phone or physical confirmation is claimed for the final reinstall. Both
that unresolved operator report and the final AP=0x4f trace prevent clean startup
qualification. The temporary `96b2e1aa` rollback and all stage-specific evidence
remain in [current status](current-status.md); final install evidence is reference
`artifacts/standby-display-20261114/reinstall-latest/result.json`.
Physical bus/INT fault injection, mixed DMA/I2C load, durable storage, long-term
wake sensitivity and electrical qualification remain open. No overnight soak ran.

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
