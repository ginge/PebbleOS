> Historical continuation audit. Installed-state and CP-sleep plans below are
> superseded by [current status](current-status.md) and the
> [CP-awake AP-only policy](ap-only-sleep.md). Findings remain preserved.

# Power continuation — 31 October 2026

This increment implements qualification controls, not a qualified production
power policy. All new switches default off. Shared/common and pinned vendor
sources are preserved. Overnight qualification remains deferred by the operator.
Evidence root: `/home/baz/Documents/ChatGPT/pebble/artifacts/power-all-20261031`.

## Subsequent clock-policy review

The [clock policy](retained-clock-policy.md) and latest
[current status](current-status.md) supersede the fixed-frequency direction below.
Retain 240 MHz while active; implement clock reduction as CP-coordinated retained
sleep. The failed fixed-120 selector and bounded probe are now removed from code
and Kconfig; archived hardware evidence confirms the vote changed both CP0/AP clocks and the
digital-voltage selector. Cold-control and bounded single-boot 240→120→240 windows
receive no unsolicited disconnect, but do not exclude intermittent failure or
state carried through earlier warm AP restores. Exact entry AP is restored,
CP unchanged, captures stopped. No new energy saving or sleep qualification.

## Subsequent SD investigation

The [SD follow-up](sd-write-completion.md) supersedes source-build and validation
claims below: a setup mask race is corrected, behavioral injection and six SD/PFS
tests pass, and fixed 240/120 diagnostics each complete five counted reconnects
with zero SD errors. Bluetooth errors remain; original SD causation is unproven.
Normal fixed source `4708694e…` is built, not installed; exact entry AP is restored.
Additional entry-AP link terminations and all evidence are recorded there. No new
saving or full storage/CPU/Bluetooth qualification is accepted.

## Final installed state and validation

The exact entry AP **`86d1dcbaaf59cc38df62e23e576d26fd485f27b1`** is restored.
Its raw SHA is `9918f4fe56fc0ee2428edcb392232007c3437832b8dcc4410c12af8c6a5a254d`;
protected SHA is `3a0893baf82a4789ef5ba3ca90456642330b0c3f4ac21ebe12d5df75f83c97de`.
The guarded AP-only rollback received its expected build, four-bit SD, existing
32 MiB PFS (`new=0`), actual 240 MHz and protocol session at 18:12:32 UTC. CP was
not written this increment; its preceding corrected Wi-Fi-off verification is
separate. Reopening PFS is not a durability/integrity proof. The operator later
confirmed dim Nadir and Bluetooth restored.

Final buffered review also finds a `0x0208` disconnect on this entry AP at
18:13:24.584 UTC. The trace ends at device time 18:14:22 without another protocol
session; the operator's later connection confirmation is not UART verification
of that recovery. The operator confirmed the phone was untouched and Bluetooth recovered afterward;
that recovery remains operator-reported rather than UART-verified.
Warnings about boot reason, a long `pindb` mutex hold and a dropped app event are
retained. Do not describe the restore trace as error-free or as soak qualification.
`entry-restore/final-decoded.json` filters attachment 5 at the recorded flash
boundary; earlier bytes in that attachment belong to different images.

All UART/power captures are stopped. UART received bytes before its intentional
stop at host 18:15:37 UTC; no overnight monitoring is running. Android Bluetooth
was left enabled. Full captured cold boot, 25 cycles for this new AP source,
storage durability and overnight qualification remain open.

Fifty relevant host tests pass (`final-default-off-validation.log`), with Ruff
and diff checks. Coverage includes mailbox wake/reply isolation and failure,
power votes, UART drain/RX/late IRQ, SD/PFS faults, ADC/battery, QMI, timer guards,
panel/display failure/recovery, batching and shared SPI/GDMA isolation. This is
the selected relevant suite, not every legacy fixture: the retired-facade DMA IRQ
fixture is not a current native-driver qualification.

The final default-off normal source builds as **`816dfa48…`**, **not installed**:
1,638,072 B FLASH (328 B free), 97,824 B RAM, unchanged 14,912 B display staging.
Raw SHA is `4d727f8c0ced95e46098013c820161110226e5ba7d61649158f473d4f02bf770`.
Resources remain `62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46`.
Release diagnostics `ada2bb4e…` and final-display candidate `7b83c7b4…` are separate
uninstalled checkpoints. The opt-in UART driver also compiles with the actual ARM
SDK command after its final flush review. Frozen configs/source records establish
each image's contents; earlier checkpoints are not the final source build.

## Implemented non-display prerequisites

- `T5AI_PM_WAKE_EVENTS`: independent four-entry scalar wake-event ring for PM
  command 0x10/channel 0x42, routed before synchronous replies. No foreign pointer
  is retained. Saturating loss is sticky; a future coordinator must drain events
  and refuse sleep after loss. This does not arm an alarm or define generations.
- `T5AI_UART_IDLE_GATING`: reacquire before MMIO, preserve RX, retain the original
  FIFO-empty flush wait, then bound the final wire drain. Gate only with RX off,
  FIFO empty and last-bit TX completion. Uncertain drain retains clocks until
  reinitialization. No CP source vote or CP console clock is removed; four-bit
  SD owns the otherwise unused AP UART pins.
- Removed experiment: the former fixed-120 selector and later bounded frequency
  probe are deleted. Startup always requests 240 MHz. Preserve actual clock
  decoding/tick calibration for CP-owned transitions and eventual retained resume.
- `T5AI_POWER_PROFILE`: existing minute callback reports actual CPU, SD software
  transitions/fallbacks/errors/completed bytes, event loss and QMI identity,
  configuration and acquisition counters. Bounded failure snapshots capture SD
  command/data/mask/control/FIFO and selected DMA residue/status before abort.
  No new periodic task, per-transfer log or I2C access for snapshots. Counters
  wrap modulo 2^32; software state is not electrical proof of gating.
- `T5AI_BATTERY_QUIET_SUCCESS`: default-off successful-sample log suppression;
  profiling still reports successes. Defaults retain original log traffic.
  Errors, ADC conversion, retries, service cadence and debounce are preserved.
- QMI snapshots capture existing identity reads and IRQ/watchdog/acquisition
  counters without changing sampling, FIFO, shake or interrupt policy. Model
  identity 05/7c is not fitted-part identification; that image was not installed.

## Controlled measurements and first failures

Matched release/PULSE images `7743c873…` (240 MHz) and `4b5d813b…` (120 MHz)
differ only in the frequency selector. Display remained fixed: dim Nadir,
USB/no cell, Bluetooth connected, notifications quiet. CP was unchanged.

| Run | Disposition | Result |
| --- | --- | --- |
| `cpu240-A` | Accepted bounded baseline | 147.965453 mW / 29.614933 mA idle at 4.996312 V; five recoveries, 44.768605 J over 290.895 s, 8.953721 J/completion |
| `cpu120-B` | Excluded whole | SD write timeout, PFS assertion and uncommanded AP reset during cycle four |
| `cpu240-C` | Excluded whole | Host hub/UART/meter outage during fifth hold, before full tail |

The accepted baseline has device-time review beyond its full tail and operator
fixed-display confirmation. Exactly five disconnects, bonded/encrypted recoveries
and protocol sessions occurred; one known ATT subscription error is retained.
USB input integration is nominal, not calibrated battery-side energy. **No new
comparative saving is accepted.** Prior Wi-Fi and SD observations remain separate.

At 17:46:46.667 UTC the actual-120-MHz candidate timed out on CMD25, one sector
`0x9b4b`, status -116 (`ETIMEDOUT`), SD `00a64480`, DMA `00040000`. The PFS
assertion caused an uncommanded AP reset. Frozen ELF maps panic LR `0x02133afd`
to `prv_io`. Original aborted raw SHA:
`41b4151bc5a6a40050ed8e612b983fb21e4aa1fd3882a1ae23e94ccde8b7a5e1`.
Complete first-failure bytes, delayed logs, phone events, configs and power are
retained. **Frequency causation is not established.** Input USB voltage alone
cannot establish internal rails or card state. The later failure-state snapshot
was absent from that image; it cannot explain the failure retroactively.
Do not extend timeouts, retry partial writes or equate DMA finish with card finish.

The return control's 17:58:44 host USB outage has kernel hub/child disconnects
and error -71; FNB58 exited 1. Five sessions before the outage do not complete
its qualification. The operator reported the watch stayed running. No firmware
cause is established for the host outage.

Temporary normal image `8fdaa366…` then booted and recovered PFS/Bluetooth but
recorded an uncommanded `0x0208` disconnect at 18:06:31 UTC, with protocol recovery
at 18:06:43. The operator said the phone was untouched. That checkpoint suppressed
successful battery logs unconditionally; final source instead makes suppression
explicitly default off. Causation is unestablished, and the subsequent entry-AP
termination above prevents attributing this issue solely to the new logging.
Its first trace is preserved in `normal-flash/`; the exact entry AP was restored.

## Explicit blockers and next actions

1. Investigate SD controller/card completion and interrupt ordering at 240 MHz,
   preserving partial-write/PFS evidence. Use failure-state diagnostics before
   revisiting CPU policy; no formatting/provisioning or masked retry.
2. Resolve unexplained Bluetooth supervision terminations using aligned phone,
   AP and CP evidence and repeat the full return workload. Preserve CP heartbeat,
   supervision and negotiated fast/idle behavior.
3. Implement a board-owned CP **applied** alarm status/owner/generation contract.
   Pinned legacy wake-config ACK precedes pointer consumption/registration and
   discards registration failure; CP sleep-vote ACK is also early. CP owns AON
   compare. Qualify consumption lifetime, cancel/fire/replacement/loss and physical
   AP wake before connecting RTC stubs. See [wake contract](wake-alarm-contract.md).
4. Establish native final-idle, interrupt, sibling-AP, retained resume and shared
   domain/PSRAM/flash ownership. Keep the application veto and SysTick intact
   until the complete contract works; no AP-only flash deep-power-down.
5. Identify fitted QMI revision and qualify WOM command/apply/ack/INT2 and gesture
   latency. Preserve 32 ms software gesture timing and shared-bus recovery.
6. Qualify local I2C/SD/UART electrically and under faults, microphone/transfers,
   mixed owners, cold power, 25 reconnects and the deferred overnight soak. Earlier
   CP-only qualification does not qualify this AP. Existing app/watchface faults
   reproduced with original CP also remain open.

## Final display phase — implemented behind default-off controls

After recording the non-display blockers, separate raw-zero brightness and
explicit display-disable sleep options were added. Nonzero brightness retains
the dim floor. Display Off/Sleep In is serialized with transfers and uses an
experimental conservative 200 ms settling delay. Wake uses vendor reset/prepare,
caches brightness, and withholds Display On until a full framebuffer write
succeeds, even if the first requested update is partial. Failed transitions retain
recovery state; updates/clear while disabled fail before SPI. No framebuffer
pointer is retained. Ordinary backlight timeout does not invoke Sleep In.
Host tests pass; physical timing, wake behavior and energy are **unqualified**.
No display experiment was included in the non-display measurements.

QSPI local bypass is already clear (byte offset 8 bit 1; native init sets reset
bit 0). Shared SYS 0x30 source release requires CP/shared-register arbitration;
no redundant local gate or uncoordinated source/domain release is added.

Historical IRQ completion was about 26 versus 20 ms at the old two-row batch.
Current native transport uses sixteen physical rows, central AP GDMA ownership
(display 0, microphone 1, SD 2/3; CP channels preserved), GDMA callbacks and QSPI
wire-completion checks. Existing completion behavior is preserved. Qualify mixed
physical owners first, then compare current-batch frame latency and energy;
changed batching alone does not justify reenabling the historical IRQ path.
