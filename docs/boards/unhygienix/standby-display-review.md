# Standby and display audit — 14 November 2026

## Current installed state

Normal AP `b40a6cb1070c5440f74c1c758b2c0eff8a38290a` is installed AP-only.
Final reinstallation reported AP=0x4f and closed sleep admission; one commanded
same-image reset then verified reset sources 0/0, SD startup, contract 0x78f and
32 clean AP returns. The 8,277-byte capture is stopped. First-install phone and
physical recovery checks passed, but were not repeated on this final pass.
Startup remains unqualified, including the earlier operator-reported long splash/
apparent self-restart. See [current status](current-status.md) for exact scope.

## Baseline and scope (pre-install checkpoint)

Production HEAD at entry: `4176671a9` (clean working tree), newer than the
handoff's `4545e34a` installation. The 13 November status and frozen
`artifacts/touch-power-20261113/flash-result.json` record installed AP
`96b2e1aabc0e5f1748c6bbdaa765efe168f1a01b`. The later touch cleanup
`9780fbc0` was built but not installed. These are historical installation records,
not a fresh identity observation. The entry build directory also held `9780fbc0`;
its images/config/dictionary were copied before rebuilding. Known-good
`4545e34a` rollback images and all earlier first failures remain untouched.

Host UART enumeration received both CH342 interfaces at ttyACM0/1. A bounded,
receive-only 460800-baud capture on if02 received 221 bytes in 30.08 seconds;
it is stopped. It did not receive a build identity/startup contract, so the
current installed AP/CP cannot be newly asserted. No reset, flash or latch
operation was performed. Operator confirms USB power through FNB58, an ADB
Android phone, and recovery by supply power cycles. Battery connection and
UART back-power paths were not separately confirmed. No electrical measurement
or overnight soak was performed.

Evidence is in reference-workspace `artifacts/standby-display-20261114/`.
That directory contains artifacts only; reference source was not changed.

## Ranked decisions

1. Reject unsupported board shutdown before teardown. Implemented separately
   from display changes. This corrects the old reset behavior, but adds no
   battery-off qualification or USB standby claim.
2. Cache successfully applied panel brightness and Display On/Off state.
   Implemented with reset/sleep/error invalidation. Dim appearance and timing
   are unchanged; repeated refresh requests can avoid SPI/clock/IRQ activity.
3. Keep the existing synchronous transfer ownership and completion lifetimes.
   No evidence supports releasing the shared QSPI source or GDMA clock here.
4. Keep raw-zero brightness and Sleep In default off. A separate operator choice
   and prepared test are required to change ordinary dim behavior or wake latency.

## Stage 1: shutdown path and circuit

Shutdown-menu -> `battery_ui_handle_shut_down` -> `enter_standby`. Low-battery,
PRF idle, manufacturing and factory-reset shutdown also call this API. Previously
Unhygienix selected the generic non-PMIC path: store reboot reason, clear/disable
display, `system_reset_prepare`, mark safe restart, set
`BOOT_BIT_STANDBY_MODE_REQUESTED`, then `system_hard_reset`. The latter forces a
whole-chip reset through T5AI, rather than releasing the power latch. The AP's
board initialization always asserts P19 again.

`system_reset_prepare` lowers services to BareMinimum and calls `flash_stop`.
The flash API waits for an in-progress erase; this is not an independently
qualified SD/PFS shutdown barrier. SD PFS writes are synchronous through native
SD/card completion, but that alone does not exclude newly arriving writers or
prove a complete power-off quiescence boundary. No latch release is added after
this incomplete proof. Storage is untouched by the new early rejection.

Boot bits use Pebble's checked retained AP SRAM (`soc/t5ai/retained.c`), not a
published vendor bootloader flag interface. The pinned vendor bootloader is an
opaque binary; no supplied source or supported contract establishes consumption
of Pebble's standby bit. No new bootloader, CP command/firmware, reset reason,
retained format or shared reset implementation is introduced.

The pinned schematic is
`boards/unhygienix/hardware/references/waveshare-t5-e1-touch-amoled-1.75-schematic.pdf`,
page 1, DC-DC AND BAT. SYS_EN/P19 drives T1 and Q3 to hold the battery boost
path; Key2 can also pull the latch gate through D5. SYS_OUT/P18 senses the
button via D4. BOOST_5V feeds VBAT_5V through Q3. VBUS separately feeds U4's
system 5V input via D3 and controls Q2. Releasing P19 does not disconnect that
VBUS feed. U1/ETA6098 and the battery boost input are upstream of the latch;
there is no demonstrated zero-current whole-board state. The vendor board
reference drives P19 low on a power-button long press; it supplies no USB-aware
shutdown, storage barrier or failure recovery. BAT_CHG/P30 is a charging-status
signal, not USB-presence detection. No reliable software VBUS sensor is configured.

Consequently `fw/board/boards/unhygienix_standby.c` now returns `-ENOTSUP` with
one warning per request, without display commands, runlevel/storage teardown,
boot-bit/reboot-reason writes or reset. Two board-conditioned build selections
replace the generic standby object; stock shared runtime source remains intact.
The menu's existing caller receives the error. Low-battery callers can ignore
it, so this is not battery depletion protection. It must not be shipped as a
qualified battery-only power-off feature.

Future battery-off implementation needs: confirmed cell/wiring and no USB/debug
back-power; a bounded exclusive storage/writer drain; transfer/peripheral
quiescence; released Key2 before final latch operation; then bounded P19 release
and an explicit failure path if execution continues (including USB). Never report
success merely because the GPIO write succeeded. After teardown, recovery must
restore a safe supply latch and restart rather than resume torn-down services.
The circuit suggests Key2 can restart from battery, and supply reconnection can
restart under USB; only the latter is operator-confirmed as an available procedure,
not a shutdown/wake observation. Do not use AP WFI, warm reset or USB restart as
power-off evidence.

## Stage 2: display and clock audit

`drivers/backlight/t5ai.c` maps percent with ceiling to 255 and a minimum raw
35. Raw zero remains default off, so ordinary timeout retains the dim floor.
`pbl_backlight_refresh` can resend the selected brightness; the light service
calls it during normal policy work. Caching is at CO5300's applied raw level,
so different percentages mapping to the same floor require no repeated write.
No shared light-service scheduler, app refresh policy or timeout is changed.
The driver adds no periodic task. Watchface time/content changes remain legitimate
redraw reasons; the framebuffer pointer is borrowed only for synchronous update.

New panel state distinguishes desired brightness, valid applied brightness and
unknown/off/on display state. A successful prepare programs desired brightness.
Duplicate successful brightness/on/off requests do no SPI work. Failed writes
invalidate the relevant applied state and identical requests retry. Every prepare
invalidates state before GPIO/reset work, including normal builds; previously
`s_prepared` was cleared only with the sleep experiment. All three reset GPIO
errors now propagate before subsequent commands. Unprepare invalidates display
and brightness caches on success or failure. The display lock already serializes
lifecycle, brightness and synchronous transfers; no cached framebuffer is added.
This reduces avoidable transactions, not periodic app redraws or proven current.

Native display uses a dedicated 14,912-byte PSRAM conversion buffer, expands
partial dirty regions to even panel windows, handles rotation in conversion,
and uses QSPI channel 0. Mic owns channel 1 and SD 2/3; CP channels remain owned
by CP. DMA completion still waits for QSPI wire idle (bounded 1 ms), with a
20 ms DMA timeout/terminate path and late-callback rejection. Bus completion
releases command/memory aperture/CS controls. QSPI bypass is already clear;
shared SYS source enable is not an exclusively owned vote. No clock, DMA, flash,
PSRAM or pin lifetime is changed by this work. Physical mixed-owner DMA, timeout
injection and QMI/touch/button interactions remain hardware qualification gates.

## Deeper panel sleep: separate, unqualified

Pinned TuyaOpen `e82840dc9b629cb77fa922fb04e3f820dc2e78b4` and platform
`13379b63e07e78770fb4d0bffe36db2754658132` retain the Waveshare module setup:
P29 reset, CO5300 quad framing, RGB565 and 6-column offset. The vendor init
waits 200 ms after Sleep Out. It is not a fitted-panel Sleep In qualification.

[CO5300 datasheet V0.00](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf),
pp. 143–146, specifies 5 ms command stabilization and 120 ms sleep-transition
restrictions/booster settling. The current experimental 200 ms waits are retained.
The module's reset/prepare waits total 540 ms (20+200+120+200), before redraw;
do not promise ordinary wake latency from a generic controller specification.
Fitted panel rails, internal reset/default behavior and valid image restoration
still need physical validation.

The default-off sleep implementation serializes Display Off/Sleep In, retries
failed disable/unprepare, resets/prepares on wake, promotes the first partial
request to full redraw and withholds Display On until redraw succeeds. Failed
redraw/on retains full-redraw/pending state. It stores no framebuffer pointer.
Tests cover these transitions, but cannot establish physical GRAM or rail state.
No new deeper-sleep feature is enabled here.

Earlier raw-zero and panel-sleep evidence is retained in
`power-investigation-history.md`, `power-continuation.md`, and reference artifacts
`power-all-20261031`, `ap-all-savings-20261108`, and
`ap-savings-dim-display-20261108`. The 8 November dim restore changed exactly the
two AMOLED flags to off. Preserve that policy choice, Bluetooth/USB first
failures and missing-milestone limits. They are not proof that panel sleep caused
a failure or that it was qualified. No earlier evidence was overwritten.

## Validation and next hardware gate

New tests compile actual production source with ASan/UBSan. They cover all
shutdown reason values without teardown; repeated brightness/on/off requests;
failed same-level retry; failed inverse command followed by restoring prior
state; unprepared desired brightness; sleep failure; every prepare SPI failure;
and every reset GPIO failure, with sleep both enabled and disabled.

Existing partial-row/rotation/full-redraw, framebuffer lifetime, SPI segmentation,
wire-drain/error/timeout/late callback, shared GDMA channel isolation, microphone,
retained-sleep/guard/vote, touch and QMI WOM tests were exercised. The initial
broader run passed 54/57; three obsolete tests (`test_t5ai_dma_irq`,
`test_t5ai_dma_deadline`, `test_t5ai_dma_fault_injection`) fail while extracting
removed `prv_pixel_stream_finish_dma` from the retired transport. The unchanged
HEAD source lacks it too. Those failures are retained in `regressions.log`;
current native SPI and GDMA tests pass. Do not describe the broad suite as clean.

No new hardware functional or electrical savings claim is made. Next is a bounded
AP-only qualification of the normal dim candidate, after exact current CP readback
and candidate/rollback verification. Capture first boot and 32 clean retained
returns; check first-motion wake, touch/hold/release, both buttons, orientation,
partial redraws, repeated explicit display off/on and phone encrypted recovery.
For sleep-policy changes prepare a separate default-off-option image and get the
operator's explicit choice before deployment. For latch-off testing first satisfy
the battery/recovery/quiescence gates above; the present USB setup cannot prove it.

Use matched A/B/A idle/content/brightness/radio/supply and fixed completed redraw
workloads with FNB58 only after stable functional checks. Record charging state,
USB/debug power paths, sample timestamps, completion counts and elapsed time.
USB whole-device energy is not panel-only or battery current; small command-count
reductions may be below meter/control drift. No numerical saving is inferred.

## Frozen normal build result

Candidate `b40a6cb1070c5440f74c1c758b2c0eff8a38290a` builds with text
1,638,083 B (+32 B), data 5,064 B and BSS 241,776 B (both unchanged versus the
entry `9780fbc0` build). Linked kernel heap is 28,960 B, above the 28 KiB gate.
The 14,912-byte conversion buffer and fixed PSRAM layout are unchanged. Image
layout/protected CRC/map and SRAM WFI/call/literal checks pass. Final CMake/Ninja
selection includes only the board standby implementation (LTO inlines the API).
Normal configuration and resources are byte-identical. This build-freeze result
preceded the installation stages below.

Raw SHA256: `d8502e8e8303fe2a3b96cbe5fc94b41f46be6dd81b4c4ec2e10064c115117f73`.
Protected SHA256: `197bd6ba6534d313e2068901cba4b35073748bb695bcd1a07a4fdcf239e197e6`.
Resources: `62b695d70c8b401ed20f12193052cb72f5a8c479ca39157b7369153051e78b46`.
Eleven focused checks pass, including 32 alternating-orientation partial-redraw
failure/recovery cycles; the broader run adds 54 passes and three obsolete-test
errors. The first link failure and first validation assumption (an out-of-line
symbol removed by LTO) are retained, with corrected final checks. Style and
whitespace checks pass. No CP image/settings, resources or storage were written.

## Installation and comparison history

Each programmer write succeeded in 32.6 seconds and selected AP only. CP97 full
readback and CRCs were verified before the first install; CP was not written
subsequently. Resources/PFS/bonds, identity/calibration and CP frequency/closed
settings were preserved throughout. Source and frozen images were retained.

| Stage | Observed evidence | Limits |
| --- | --- | --- |
| First latest install | Exact `b40a6cb1`, reset sources 0/0, SD existing volume, contract 0x78f, 32 clean returns, encrypted bonded protocol recovery; 8,485 bytes. | Operator long-splash/apparent self-restart report remains unresolved; one candidate boot banner captured. Initial BLE 0x23e and parameter retry retained. |
| Previous AP comparison | Exact `96b2e1aa`, reset sources 0/0, contract and SD; 2,231 bytes. | No extra agent reset, no new 32-return/phone milestone or operator comparison result. |
| Final latest reinstall | Exact `b40a6cb1`; initial AP=0x4f/CP=0 admission failure; one commanded reset then 0/0, SD, contract and 32 clean returns; 8,277 bytes. | No new encrypted phone/physical confirmation. Commanded recovery does not qualify initial startup or resolve the earlier report. |

Reference artifact root: `artifacts/standby-display-20261114/`. Results are
`install/flash-result.json`, `restore-96b2e1aa/result.json` and
`reinstall-latest/result.json`; final first-boot bytes remain in
`reinstall-latest/first-failure/`. Every capture is stopped. The preceding AP=0x4f
boot caused by the initial CP readback is separate from candidate startup.
No shutdown or deeper-panel-policy test, electrical measurement or overnight
soak was performed. No reset-source root cause is inferred.

## On-but-dim follow-up

The current compositor returns without a transfer when its framebuffer is clean
and forwards a dirty-row range to the display driver. Native CO5300 expands that
range to even physical windows. The driver sends requested updates; it does not
run an autonomous frame-refresh timer. App ticks/animations or conservative
invalidation can still cause avoidable
rendering, PSRAM conversion and QSPI/DMA work; profile the actual selected face
before changing policy. No redraw-cadence change was implemented in this increment.

First pursue unnecessary redraw/command removal and balanced local ownership
while preserving current visible content and wake sources. Suppressing seconds
or animations, reducing the dim floor or using panel idle modes needs an explicit
behavioral choice and separate validation. Less host work can permit more AP
retained idle, but does not establish reduced panel scan/emission power. Keep
shared QSPI source/GDMA/flash/PSRAM active until ownership and restoration are
proven. Matched idle and energy-per-completed-redraw measurements remain open.
