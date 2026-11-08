# Unhygienix current status — 8 November 2026

The active direction is **CP-awake AP sleep plus local peripheral clock
lifetimes**, with the original dim display behavior. CP deep sleep is deferred.
The board enables the existing savings by default; hardware qualification remains incomplete.
Historical checkpoints are preserved in [the power history](power-investigation-history.md)
and [the CP sleep history](coordinated-sleep-history.md).

## Installed firmware

| Item | Verified state |
| --- | --- |
| AP | `f1f862e83489b050a27b1e24ef29e2e9ce5938f2`, flashed 8 November at 10:06 UTC |
| AP raw SHA256 | `c6e4d18d9b3f6191bbee16ac60944f4de076ebce33942f8ef5f23d2fd45ee750` |
| CP | Calibrated Wi-Fi-off alarm server CP97; raw SHA256 `c02c2236f210aa2a2c69b303aa59a4471990656b0e7be31d008d1d83fd6a00fe` |
| CP protected SHA256 | `f114a5abbbc18e8f2a6ae4aa8f25653fe11f5425e418054b6d955ca8daf238c4` |
| Resources | SHA256 `62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46`; unchanged |
| Display | True-zero brightness and explicit panel Sleep In disabled; original dim floor |
| Active CPU policy | Request 240 MHz; no fixed 120 MHz experiment |

The last AP write selected no CP, resource or PFS partition. Full actual CP
readback and protected CRCs matched before that write. Bonded encryption and
protocol service were received at 10:08:19/10:08:21 UTC after one companion restart.
The bounded captures are stopped; no overnight soak or ongoing monitor is running.
The later source cleanup has not been flashed over this image.

## Implemented policy

- AP retained idle uses an owned, generation-aware CP alarm. CP keeps its APP
  sleep veto; shared core/flash/PSRAM clocks and voltage remain active.
- The AP rechecks kernel idle, deadlines, peripheral/IPC ownership and pending
  interrupts, suspends SysTick, executes the wake gate from SRAM, restores routes,
  verifies retention and accounts elapsed AON time exactly once.
- Every return rejects any increase in CP low-voltage, resource, clock or native
  mask-barrier counters. Unknown controller state or restore failure closes sleep.
- I2C, SD and AP UART local bypass lifetimes are enabled in the installed profile.
  Uncertain drains/faults retain clocks. Shared GDMA and CP UART source votes are
  preserved. Battery-success logging is suppressed; conversion and error behavior
  are unchanged. Existing I2C receive FIFO remains enabled.
- The board-default `T5AI_POWER_SAVINGS` configuration selects this non-display
  profile. Production retained idle requires CP-awake mode. Bounded CP experiments
  are separated in `boards/unhygienix/Kconfig.power-debug`.

See [AP-only implementation and build instructions](ap-only-sleep.md).

## Evidence and its limits

| Evidence | Result and limit |
| --- | --- |
| AP-only bounded control | 12 AP returns, zero CP transition deltas, aborts, deadline misses or contract faults; post-test ADC and operator-confirmed Nadir/BT recovery |
| Combined continuous AP `efca4e76…` | 32-return milestone received, all CP transition deltas/fault 0; display wake/update confirmed |
| Current dim AP `f1f862e8…` | Exact build/contract and protocol recovery received; only the two AMOLED flags differ. Its 32-return activity milestone was not received during the short capture |
| Wi-Fi off | Earlier nominal USB on/off/on reconnect workload showed 1.7225% less energy/completion than mean controls; bounded observation, not calibrated battery life or qualification of this AP |
| SD local gating | Bounded functional evidence; measured differences were smaller than control drift. No reliable saving demonstrated |
| Current combined policy | No accepted matched idle or completed-workload energy comparison |

The installed experimental profile was explicitly requested despite incomplete
qualification. Enabled configuration is not proof of clock gating or runtime
residency. Do not turn the earlier 12/32-return observations into qualification
of the current dim image, a CP deep-sleep claim or a measured battery saving.

## Cleanup validation

The board-default release build is `c162b6d4…`, configured without a power-profile
command-line override. Its configuration exactly matches the previously tested
savings profile. The linked SRAM audit passes and resources remain unchanged.
`CONFIG_T5AI_POWER_SAVINGS=n` is the explicit control opt-out. The runtime policy
is committed in `1c3f54198`; no cleanup/default-change build has been flashed.

The pre-default-change savings release build is `53e61e12…`; the explicit-off control is `e1ba2fc9…`.
Those cleanup builds passed layout, protected-image and resource-size build checks, with the
resource hash above unchanged. The savings configuration matches the installed
dim revision apart from the new umbrella profile flag. Its linked AP WFI gate
and PC-relative literals are in SRAM; its only call targets the SRAM reset helper.
These cleanup builds have not been flashed.

Clang-format 22, Ruff and whitespace checks pass. All 155 affected native fault,
ownership, timer, alarm and packaging tests passed with zero skips, including
CP instruction emulation. Exact logs are in the cleanup artifact record. The broad `pbl test --no-images` build stopped
on a host nanopb-generator dependency problem and an untouched pdc2png C23
const-pointer compile error. That full shared suite is not recorded as passing.

An isolated rebuild of the formatted CP source produced byte-identical app.bin,
sdkconfig, calibrated raw package and protected package. CP provenance hashes
now match the formatted source; controller/calibration/ABI payloads are unchanged.
Every paragraph of the three previous status/TODO journals remains in the
investigation history; the exact originals are also in the cleanup backup.

## Release blockers and next actions

1. Resolve Bluetooth discovery failures, connection-parameter timeouts and
   intermittent drops. Preserve the existing first-failure captures; they also
   occurred in normal controls, so causation is not established.
2. Record actual AP sleep entries/blockers with dim Nadir and fixed supply.
   Measure matched idle power and energy per completed workload.
3. Qualify SD/PFS durability, shared I2C recovery, microphone/SD/display DMA
   coexistence, deadlines and watchdog behavior on the combined profile.
4. Repeat 25 consecutive reconnects and a fully captured all-power-removed cold
   boot with bond/face restoration. Earlier CP-only results do not qualify this AP.
5. Complete the explicitly deferred overnight connected soak with UI interaction.
   Externally triggered AP resets remain release blockers under board instructions.

Sensor WOM/touch idle policy, CP console clock lifetime, shared GDMA/domain votes
and memory retention remain separate implementation work. Display changes remain
last and the user's dim policy is preserved. See [the ordered TODO](power-todo.md).

## Reproduction and evidence

The exact installed image/config/source and received data are retained in the
reference workspace under `artifacts/ap-savings-dim-display-20261108/` and
`artifacts/coordinated-sleep-radio-20261031/ap-savings-dim-display-20261108-built/`.
The preceding combined image and Bluetooth first failures are under
`artifacts/ap-all-savings-20261108/`. Cleanup backup, formatter/test/build results
and CP payload-equivalence proof are under `artifacts/power-production-cleanup-20261108/`.
Production source remains this repository; these artifact paths are evidence,
not prerequisites hidden inside the production implementation.
