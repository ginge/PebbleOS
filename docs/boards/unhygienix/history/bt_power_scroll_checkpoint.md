# Unhygienix: Bluetooth, power and scrolling checkpoint

> Current project status (22 October 2026): [implementation, completed work and remaining tasks](../current-status.md). Dated results below retain their original evidence limits.

This retained report records dated observations, not current operating instructions.
Use the [operator handoff](../index.md) and [tooling guide](../tooling.md)
for current procedures. Obsolete commands and machine-specific paths have been
omitted. Artifact labels are descriptive; external captures are not distributed
with this repository. Recorded dates, measurements and SHA-256 values are retained.


This checkpoint records the fixes and hardware observations on 2026-10-03.
It supersedes earlier Bluetooth-recovery and runtime frequency-governor notes.
Ten-hour connected-idle stability is now operator-reported as passing; broader
reconnect, cold-boot and end-to-end 30 fps validation remain open.

## Bluetooth: configure the physical RF path after initialization

Repeated failures reported supervision timeout `0x208` (HCI `0x08`). CP
scheduling and advertising bookkeeping continued, but synchronization stopped.
HCI/mailbox timeout, exhaustion and fault counters stayed zero. Host HCI captures
saw nearby advertisers but no watch advertisements in the failed state; the same
adapter saw the watch after reset. Healthy and failed states both showed the
cores at 240 MHz. These observations did not support lowering the CPU clock as
an established fix.

The vendor command `rfconfig ble_btpll` changed the effective RF configuration
from `0x0101` to `0x0202` and restored bonded PPoG without resetting the watch or
deleting bonds. The current fix calls that physical configurator immediately
after successful Bluetooth initialization in the existing CP HCI-reset thread.
The precise internal RF/PLL failure mechanism remains unproven.

Source: [CP hook and build instructions](../../../../boards/unhygienix/tuya/cp/README.md),
`boards/unhygienix/tuya/cp/ble_rf_init.c`, and `tools/patch_unhygienix_cp_rf.py`.
The tool verifies the pinned stock CP image, appends the compiled hook and
redirects one Bluetooth-init call. Existing code/data addresses and the AP ABI
stay intact. Initialization failure returns without changing RF. This is a
source-backed binary integration, not a complete vendor CP source rebuild.
Data-initializer and RF-mode-callback candidates were rejected: neither retained
the required physical configuration, and the callback candidate still timed out.

The installed CP image logged `Pebble BLE RF PLL: config=0x0202` and established
bonded encrypted PPoG at 21:18:05 London time. It remained connected through
21:33:15, with no disconnect, assertion or uncommanded AP reset; effective RF
configuration remained `0x0202`. A commanded hardware reset at 21:33:58 reapplied
the configuration and restored bonded encrypted PPoG at 21:34:20. This reset
was not a verified power-off cold boot. The operator later reported roughly
10 hours of stable connected operation with no disconnects after the installed
RF-initialization candidate, which is a passed long connected-idle observation.

## Power: Round 2 active/idle model

`soc/t5ai/power.c` requests 240 MHz once through CP frequency arbitration.
`soc/t5ai/freertos.c` derives SysTick from the accepted hardware clock and uses
`DSB/WFI/ISB` in scheduler idle. A successful request is latched; failed startup
requests permit retry. Clock decoding handles the actual source and divider,
rather than assuming a requested clock was accepted.

This follows Round 2's active-clock and scheduler-idle model. The previous
activity-based frequency governor and its display/touch hooks were removed.
CP frequency votes affect shared clock/voltage arbitration, so they must not be
treated as independent AP-only frequency changes. Idle does not lower the
240 MHz clock. Deeper sleep, wake alarms and measured energy savings remain
unfinished or unvalidated.

## Scrolling: display transfer and touch backpressure

`src/fw/drivers/display/t5ai/display_co5300.c` pipelines DMA row batches through
two cache-aligned buffers, reducing full-frame transactions from 466 to 260.
Observed warm transfer time was approximately 19 ms at 240 MHz and 26 ms at
120 MHz, with no observed DMA failures. Transfer fits a 33 ms frame budget;
this does not establish complete rendering at 30 fps. Settings rendering still
measured approximately 72–130 ms, with CPU blending a remaining bottleneck.

`src/fw/drivers/touch/cst92xx/cst92xx_t5ai.c` paces delivery at 33 ms and checks
App, Worker and kernel queue pressure. It retains the latest position while
queues are busy, defers the final position and release until capacity is
available, and delivers a pending release before a fresh contact. This fixes
the touch-event queue overflow that caused an App assertion and reboot. Idle
touch remains interrupt driven. The operator confirmed responsive repeated
Settings scrolling after this fix. That observation predates the final CP PLL
image; combined scrolling/Bluetooth validation on that image remains pending.
The separate earlier App privileged-code stall remains unresolved.

## Validation and retained evidence

The power, touch, kernel, hardware, flash and CP RF suites passed 55 host tests.
CP source/image safety checks also passed; overlapping suites must not be counted
as additional distinct tests. Current hardware images retain profiling/trace
instrumentation and are diagnostic builds.

Artifacts are retained in the reference workspace's `logs/` directory:

| Artifact | Purpose |
| --- | --- |
| `bt-round2-raw-hci-failed-20261003.btsnoop` | Failed-state host advertisement capture |
| `bt-round2-control-hci-20261003.btsnoop` | Same-adapter healthy control capture |
| `bt-radio-init-pll-candidate-20261003.json` | Installed candidate and validation record |
| `bt-radio-init-pll-15min-soak-20261003.bin` | Connected run before commanded reset |
| `bt-radio-init-pll-validated-20261003.bin` | Run and bonded reconnection after reset |
| `bt-cp-ble-init-pll-20261003-app.bin` | Installed raw CP image |
| `bt-cp-ble-init-pll-20261003-app.crc.bin` | Installed protected CP image |

Installed raw CP SHA-256:
`2695ac23a57455ce8a5a791cb43a7a6fc3d9b111cb81fa2506a72678555ad10d`.
Protected CP SHA-256:
`f037098b1fd424dd18191fe3c39e26cc31d3f2a68ebe1b0477461039fa2f2cf6`.
The protected payload occupies `0x22000..0x11df20`, below AP start `0x132000`.

Remaining qualification: post-cleanup watchface install/launch regression,
25 disconnect/reconnect cycles, and a true cold boot with bond and selected-face
restoration. Also complete the final-image scrolling check and measure
end-to-end frame rate. Retain the complete first failed session. Any
uncommanded AP reset remains a release blocker.
