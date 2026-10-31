# Unhygienix Wi-Fi optional-feature investigation — 2026-10-30

## Automated workload checkpoint — 15:19 UTC, 31 October

Corrected Wi-Fi-off CP **`d4d65559…` is currently installed**, with unchanged AP
`c656055a…`, for consecutive reconnect qualification. Its expected AP startup is
received. The first pre-qualification readback handshake timed out after
commanded resets; its complete trace is preserved, with no received CP crash and
normal existing-PFS/bonded-session recovery. A fresh-name retry read all 1,114,112
bytes successfully: active CRCs and exact corrected-off package bytes match, and
the full physical SHA is `4f2fd6448da1a78d9c918676faccfb4c103dc08df0cdb6fbfa62a829fc0ebd6c`.
`adb-off-25-A` completed at 15:15:17 UTC after all **25 consecutive cycles**
and the full recovery tail: exactly 25 disconnects, bonded/encrypted recoveries
and protocol sessions, no extra termination or new error class detected by the
harness. The logger closed 0 at 15:15:20. Four notifications and one known ATT
subscription error were recorded during settling; this functional run's energy
is excluded from fixed-display comparison. The operator confirmed Bluetooth
connected and dim Nadir restored/unchanged. Delayed review extends through device
time 15:15:56, after the 15:15:17 batch end, with no new error class, fault/reset,
app transfer or extra termination. The counted functional gate is complete;
full hardware acceptance remains open. The physical cold-power check is armed
and requested in `cold-off-A`; no fresh boot or operator power-removal result
has been recorded yet. Overnight observation has not started and is deferred by the operator until
a later overnight window; it is not a passed or waived qualification gate.
No experimental image is promoted; production defaults, AP/display policy,
common source and pinned vendor trees remain unchanged.

The controlled on/off/on sequence completed five encrypted/bonded protocol
recoveries per batch, after 90 s quiet settling and identical 5 s user-disabled /
40 s on holds plus a 60 s tail. The operator confirmed dim Nadir throughout each
completed run and Bluetooth connected afterward. Nominal USB measurements:

| Profile | Elapsed | Energy | Energy / completed reconnect | Mean power |
| --- | ---: | ---: | ---: | ---: |
| On A | 293.014 s | 45.586809 J | 9.117362 J | 155.570449 mW |
| Off A | 291.665 s | 44.996220 J | 8.999244 J | 154.265703 mW |
| Return on D | 291.861 s | 45.982879 J | 9.196576 J | 157.540354 mW |

Off uses **0.157725 J/completion (1.7225%) less than the mean flanking controls**.
Return-on drift is **0.079214 J/completion (0.8688%)**. Supply means are
4.988343 / 4.997812 / 4.997622 V. Report elapsed time and mean power as well as
energy; this is a small bounded USB observation, not calibrated savings,
battery-life evidence or full hardware qualification. The clean idle pair is
148.091 / 150.332 mW (2.240 mW, 1.490% lower off), not a quiet idle ABA.

Late CRC-valid device-time review extends beyond the completed return window and
finds no notification, transfer, fault or reset inside the three workloads; all
have the same ten radio parameter updates. Each also records one `No subscription
found for ATT handle 3` error before successful protocol recovery. Do not call the
stack error-free. Return B was excluded whole after a notification; return C
stopped during settling before power or Bluetooth work. Their first traces are
retained. The operator paused that notification source before quiet return D.

Actual full CP readbacks after the initial on and off workloads match active
protected CRCs and exact regenerated raw/package bytes. Programmer reads are
commanded warm reboots, never cold-power checks. `automated-workload-comparison-3.json`
and `buffered-workloads-review-1449/` retain the accepted sequence and late review.
The counted batch is complete; true all-power-removed cold boot,
PFS/application integrity and overnight interaction remain open. The mixed UART
experiment stream has actual received data; it is not an overnight qualification.
Original CP `1713dee…` remains the exact rollback. Earlier current-image statements
below describe their historical phase, superseded by this checkpoint and
`hardware-phases.jsonl`. Display remains last.

## Earlier idle/application experiment sequence — 31 October 2026

Corrected Wi-Fi-off CP `d4d65559…` boots the unchanged AP `c656055a…`, reopens
existing SD PFS (`new=0`) and reconnects bonded/encrypted Bluetooth. Its 120-second
quiet, dim-Nadir idle window after 90 seconds settling averaged **29.681765 mA /
148.091477 mW**. The exact original CP `1713dee…` was restored for the experimental
control failure and baseline comparison below. The automated checkpoint above
identifies the later corrected-off installation. USB power/no cell and display
configuration remained fixed. These are bounded experimental observations, not a qualified production
selection or battery-life result. Normal CP defaults and runner remain unchanged.

The first off power window is excluded whole: the operator transferred/launched
an app, accompanied by a notification and two app faults for UUID `873ef5ea…`,
PC `0x6e69616c`, LR `0x1b0d`. Preserve that trace and compare the same operation on
the control before attributing the faults. The later automated checkpoint closes counted reconnect work and its bounded
energy comparison; PFS/application integrity, true cold power and deferred
overnight interaction remain gates.
Display work remains last.

### Idle measurements with fixed display

| CP profile / window | Mean current | Mean USB input power | Nominal energy |
| --- | ---: | ---: | ---: |
| On A, secondary | 30.081040 mA | 150.087340 mW | 4.992906 mWh |
| Off B, quiet primary | 29.681765 mA | 148.091477 mW | 4.928155 mWh |
| On B, secondary | 30.059213 mA | 149.972723 mW | 4.990759 mWh |
| On C, quiet primary | 30.131727 mA | 150.331772 mW | 5.002707 mWh |

Each measured window is 120 s at about 4.989 V. Device-time review found on A's
last notification only 37.6 s before recording, and on B's notification/image
drops only 69.3 s before recording. Neither meets 90 s quiet settling. Both are
secondary evidence; **the earlier primary ABA calculation is superseded**.
The quiet off B/on C pair shows **2.240296 mW (1.490%) lower with off**. Its
one-second mean sample standard deviations are 1.487 / 3.434 mW. This descriptive
single-pair result has no independent calibrated uncertainty or long-term/workload
qualification; no battery-life or qualified saving is established. No spike trimming.
The meter's 0.15 W display agrees with roughly 150 mW; mV is voltage.

Quiet UART can buffer logs for over two minutes. Per-read host timestamps do not
identify event time. A read-only PULSEv2 decoder verifies COBS, CRC and lengths,
and extracts embedded RTC timestamps to review settling and measurement activity.
`idle-device-timestamp-review.json` and `idle-quiet-pair-comparison.json` supersede
the retained initial host-time calculation. RTC/host offset still limits precise
workload timing; do not assign exact transfer energy from host arrival alone.

### Application workload blocker and original-CP control

The operator reports transfer requires launch; the loading/app screen appeared.
The on attempt is excluded from fixed-display workload energy. Validated logs
show UUID `873ef5ea…` fetched with cleanup result 0, Pebblecraft launched, and
later watchface UUID `16183a4f-c48d-4702-8a48-1211031a8115` faulted at PC `0`,
LR `0xc89`, with automatic relaunch. The operator did not see a visible fault.
The full first control-failure trace and preceding phase are frozen with hashes.

Exact original CP `1713dee…` was restored CP-only; received AP `c656055a…`,
existing SD PFS (`new=0`) and bonded/encrypted PPoGATT startup are verified. The
operator repeated the same app operation and restored Nadir. CRC-valid logs on
that original CP reproduce **both** Pebblecraft `0x6e69616c/0x1b0d` and watchface
`0/0xc89` signatures. Wi-Fi removal is not required to trigger either. This is
reproduction evidence, not a root-cause diagnosis or proof the CP cannot contribute.
Preserve `original-cp-first-workload-failures*` alongside both experimental failures.
These are app faults; no uncommanded AP reset is established in the bounded trace.

Original CP was restored for the baseline reproduction. The newer automated
checkpoint above identifies the current image. Neither experimental image is promoted.
Counted reconnect work with dim Nadir fixed was the next workload alternative;
the automated checkpoint above records its completion and programmed-byte proofs.
App usability/PFS integrity, true all-power-removed cold boot and deferred overnight
periodic interaction remain gates. Do not claim an overnight pass from the mixed experiment stream.

### Counted reconnect alternative

Earlier original-CP disconnects at 13:29:51 and 13:31:06 (encoded `0x0208`) were
operator-confirmed Bluetooth toggles or range changes; both recovered to encrypted,
bonded protocol sessions. Do not label them spontaneous failures.

A planned five-cycle original-CP test used a fixed 240 s window beginning
13:37:24.378 UTC. It contains five disconnect/encrypted/protocol completions, but
a sixth disconnect at 13:41:19.387 (`0x0215`) before the window ended; its session
opened at 13:41:36.027 afterward. The operator confirms five manual cycles with dim Nadir unchanged.
The six link disconnects comprise five peer-power-off (`0x0215`) and one
remote-user termination (`0x0213`); this does not mean the operator performed six
manual cycles. The last manual-cycle recovery finished after the window boundary.
ADB-timed cycles were the next concrete measurement step at that point. The
later automated checkpoint records the authorized USB/ADB runs; no new watch
pairing was introduced.

The 24,002 samples nominally integrate to 38.490365 J, but **the whole window is
excluded from controlled workload-energy acceptance** because the defined batch
contains extra unfinished work. Do not trim it into a successful five-cycle trial
or claim saving from dividing that energy by five. No validated app fault or AP
build/boot log appears inside the defined window. This is original-CP evidence,
not 25-cycle candidate qualification. All power recordings stopped cleanly.

### Preserved calibration failure and repair

Initial off `e48465c3…` failed before AP startup: CP MemFault, PC `0x0`, LR
`0x0205d1f7`, in `bk7011_get_rfcali_mode` during `bk_cal_if_init`. PHY OS callback
offset 64 (`_get_info_item`) was NULL under the vendor Wi-Fi guard. The full
913,127-byte failed phase, including repeated CP boots, is retained as
`off-first-failure-complete.rx.bin` with SHA/offset metadata. Exact original CP
`1713dee…` was restored and dim Nadir/Bluetooth recovery operator-confirmed before
the corrected trial. This failed phase is not a soak or reconnect pass.

The existing `NET_PARAM=y` already compiled pinned `get_info_item` and
`save_info_item`; the board overlay now retains their callback assignments
independently of Wi-Fi. Their no-override result preserves the actual flash/OTP
calibration fallback. No factory calibration data is fabricated. Linked guards
require exact callback symbols at table offsets 60/64. ARM tests execute the
previously failing calibration path with absent/present OTP, reproduce PC=0 for
a null slot and reject a wrong binding. **25 focused host tests pass**.
Corrected off raw/config outputs reproduce across separate clean source builds;
on output is byte-identical after the callback change.

## Source-built off/on profiles — 31 October 2026

This increment supersedes the older build-blocked/no-CP-write dispositions below.
Entry production HEAD is `26cdf22c6` (the timer work was committed by the operator).
Changes remain board-owned: staged overlays, CP profile/build/package tools and
focused tests. Neither pinned vendor submodule nor common Pebble source changed.
AP/display configuration, resources, storage geometry and pairing storage are fixed.

### Implemented and host validated

`tools/unhygienix_cp_build.py` defaults to the **off build profile** and stages a
fresh archive of the two verified pinned SDK inputs. It requires GCC
10.3-2021.10 and CMake 3.31.6, generates the complete matching TuyaOpen header,
and retains compiler/source/config/output provenance. Hidden TuyaOpen
`ENABLE_WIFI` remains a board capability; actual CP feature flags select behavior.

The off profile disables Wi-Fi, VNET, CP Wi-Fi IPC, lwIP and Wi-Fi AT support.
The on control preserves the corresponding pinned features (Wi-Fi CP IPC stays
off in both). Conditional dependencies remove the CIF customer placeholder,
network adapter and mbedTLS network socket backend independently of HCI/SYS IPC.
Crypto otherwise remains enabled. Armino discovers dependencies before CONFIG
values exist, so the explicit profile is also supplied during that early phase;
effective CONFIG is checked again after the link. Bluetooth uses the same pinned
controller archive; off selects `libcom_phy.a`, retaining Bluetooth calibration,
wake and shared RF votes. Wi-Fi-only PHY queries/DSSS callbacks are guarded, and
`libble_wifi_exchange` is excluded. Memory/partition ownership is not reclaimed.

The existing RF correction is called directly from the staged HCI-reset source.
`tools/unhygienix_cp_variant.py` verifies the pinned stock input, exact reviewed
raw/config hashes, controller archive SHA, every instruction and relocation in
the scheduler/TX/max-latency functions, and both parameter-application calls.
It compiles the existing latency correction against the new ELF's verified
symbols. The compiled mailbox/HCI/PM contract matches the native AP. Unknown
images remain rejected; the legacy fixed-hash/address packagers and normal runner
were not weakened or relabelled. Experimental schema-2 packages are separate
from normal `t5ai_cp` packaging and remain `flash_qualified: false`.

A clean on build exposed missing dependency propagation, fixed in the board
recipe. The first library check also confused the retained `bk_phy` adapter with
the proprietary Wi-Fi archive; it now examines only vendor `bk_libs` paths.
A static `app_wifi_init` may be inlined; the on check requires the externally
linked Wi-Fi/CIF/TCP-IP services, while off rejects all four names.

Reconfiguration duplicated the `-lm -lgcc -lc -lnosys` groups and changed GNU ld
veneer ordering. The canonical link records relative work paths and collapses
only immediately repeated complete runtime groups. The pre-guard off/on links
then matched raw SHA and command across two independently compiled directories.
These were actual differing outputs, not exact stock reproduction. Final guarded profiles have fresh source builds and reviewed hashes; corrected
off raw/config outputs reproduce across separate clean directories and on remains
byte-identical after the callback repair. Quota failures and superseded binaries remain frozen. Disposable staged
SDK copies were removed only after retaining their outputs/logs/overlay sources.

### Preserve the installed flash contract

The pre-write CP-only readback received all **1,114,112 physical bytes** and
matches the recorded installed latency-fixed CP's protected prefix. Its active
993,564-byte logical image hashes to
`1713dee17dd98cd8aea8114a438d24ed87112585c16c45960221c4b96f344efb`;
active CRCs pass, and the full physical backup retains unreferenced tail bytes.
The read command caused a commanded warm boot, not a cold-power qualification.

That installed CP also contains controller deadline/ACK protections. The first
stock-derived packages lacked them and were **not flashed**. The final board
`flash_contract.S` carries forward RAM launch/wait, the five-second independent
32 kHz AON bound and the existing fail-stop/reset reason. Staged flash low-level
waits and launches use it; notification errors, elapsed timeout and iteration-cap
exhaustion without a real ACK fail-stop before proceeding. Stock controller and
protection operations remain; paused SPI/volatile/ownership experiments and their
old diagnostic counters were not restored. The boot copy table and raw ITCM load
bytes are verified, not just symbol presence.

The 25-test final suite includes native execution of the actual staged
notification code with immediate/delayed/missing/wrong ACK, write failure,
powered-off AP and stalled-time cases. Unicorn executes both profiles' linked
RAM launch/wait, timeout and counter-wrap paths, checks the fail-stop register
writes and rejects instruction fetches outside ITCM while waiting. The controller
replay covers both interval-application paths, wrap, the captured failure and
normal latency behavior. Hardware behavior and PFS integrity still require tests.
Ruff passes. clang-format 21 was used with unknown-key warnings for two newer
repository style options; the repository style file was preserved.

| Guarded profile | Base FLASH | Static RAM | ITCM | Packaged logical bytes |
| --- | ---: | ---: | ---: | ---: |
| off (corrected) | 349,752 B | 63,580 B | 2,648 B | 349,788 B |
| on | 971,768 B | 122,940 B | 16,144 B | 971,804 B |

The on ITCM region has only 208 B headroom. These are image sizes, **not energy
savings**. CP PSRAM heap, AP reservations and flash partitions remain unchanged.
Exact reviewed base SHA: off `0ce05f25…`, on `438d65ca…`; packaged SHA: off
`d4d65559…`, on `41b823a4…`. Earlier off `e48465c3…` is rejected. Full hashes are in the board link contract and
frozen package manifests.

### Device experiment and remaining gates

The dedicated experiment script regenerates and compares the complete package,
then validates vectors/CRC and the exact readback-matching rollback before each
CP-only write. On occupies `0x22000..0x11e162`; corrected off ends at `0x7cbc6`;
both stay below AP `0x132000`. A bounded early-fault watcher restores the exact
original CP if a startup CP fault is received. This does not replace subsequent
supervision, PFS and reconnect qualification.

All CP operations are phase-marked with UTC and UART offsets in
`hardware-phases.jsonl`. The received stream `baseline-soak-v3` includes commanded
read/flash resets and the first failure; it is not an uninterrupted overnight pass.
Preserve received bytes and first failures, and do not infer health from a merely
waiting capture process.

Matched idle and completed reconnect-workload observations, exact programmed CP
bytes and 25 consecutive reconnects are recorded in the automated checkpoint
above. Whole contaminated windows are excluded. Storage/application integrity,
all-power-removed cold boot and deferred overnight connected idle with periodic
UI interaction remain qualification gates. Any uncommanded AP reset blocks
acceptance. Retain original CP rollback `1713dee…`. Production default-off and
normal-runner integration remain pending these gates. Subsequent non-display
phases follow the ordered power TODO; display is last.

Evidence: `/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-off-implementation-20261031/`.
It includes failed first attempts, source snapshots, ELF/bin/map/config and link
commands, pre-write full CP readback/identity, package proofs, tests and phase logs.
The older sections below retain their dated historical evidence limits.

## Original investigation disposition

Investigation complete; **default-off implementation blocked, electrical and
device qualification pending**. Native PebbleOS AP already has no Wi-Fi stack.
The supplied CP does initialize Wi-Fi without an AP Wi-Fi client. Its pinned
runtime cannot fully deinitialize Wi-Fi. Vendor source offers a Bluetooth-only
compile path, but it requires a new CP build and a supported packaging contract;
the present hash-checked binary-hook pipeline cannot accept that build.

No firmware behavior, common Pebble source, vendor source, installed firmware,
build configuration or original build output was changed. No power saving is
claimed. A board Kconfig switch alone would misleadingly report Wi-Fi off while
the CP still initializes it, so none was added.

## Later power continuation — 2026-10-31

[The non-display continuation](non-display-power.md) supersedes the tooling and
installed-image assumptions in the earlier revalidation below. CMake 3.31.6 and
the SHA-256-verified vendor GCC 10.3 toolchain are now provisioned under `/tmp`.
The recovered hardware-generated TuyaOpen header and build parameters match the
pinned TuyaOpen/T5AI commits and Waveshare board. The isolated Wi-Fi-on CP now
completes compilation, ELF link, binary generation and size report with verified
GCC 10.3 and Python 3.12. A staging-only CMake map-filename correction is retained;
production vendor sources remain unchanged. Effective CONFIG values match the
archived pinned stock, and both binaries are 971,156 B, but SHA differs: 1,467
bytes and 169 linker-veneer symbols differ. Non-veneer symbol addresses/sizes/types
match. This is build recovery, not byte-identical stock reproduction.

Evidence and the executable environment probe are in
`/home/baz/Documents/ChatGPT/pebble/artifacts/tickless-wfi-20261031/cp-config-recovery/`
and `cp-on-linked/`. No off variant, variant-aware guarded packaging or CP device
qualification passed; no CP was flashed. Preserve the fixed-hash rejection of
this relink until source/ELF-verified RF/latency integration and Bluetooth-only
PHY/calibration/service checks are implemented. Non-display AP-only experiments
preserve the installed CP.

## Revalidation and measured setup reading — 2026-10-31

This section supersedes the earlier session's tool/hardware availability claims,
not its frozen firmware identities. Production HEAD is
`2eacc9eaa9682f9e30e72abf1211b8675ecdef67`; the existing local change to
`tooling.md` was preserved. Repository/board/workspace instructions, the power
TODO and current status were read. No firmware source or build default changed.
Default-off implementation remains **blocked**; device/electrical qualification
remains **pending**. No firmware was flashed and no other power work was begun.

### Rechecked ownership and integration dependency

The pinned T5AI revision still matches `sdk.lock.json`. Re-extracting the archived
CP ELF reproduces the stock binary SHA-256
`0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856`.
The native AP still has no Wi-Fi stack. CP `bk_init()` still calls
`app_wifi_init()` before Bluetooth, and `wifi_init()` acquires MAC/PHY power,
MAC/PHY clocks and `RF_BY_WIFI_BIT` before creating the Wi-Fi tasks. Both deinit
entry points remain unsupported. These establish a normal-boot executable path
and attempted acquisition, not current runtime votes, successful initialization
on this boot, or a measured Wi-Fi power cost.

Bluetooth-only selection retains PHY/RF adapters when Bluetooth is enabled.
`nm` confirms both `libcom_phy.a` and `libcom_phy_nspe.a` export
`bk_cal_if_init` and `phy_wakeup_for_bluetooth`. The required library is present.
The unresolved dependency is a working CP project and verified packaging:

- `projects/tuya_app/cp/CMakeLists.txt` unconditionally requires `controller_if`
  and `lwip_intf_v2_1`. Its `cp_main.c:user_app_main()` unconditionally calls
  `cifd_cust_msg_init()`; `customer_msg.c` registers its callback with
  `cif_register_customer_msg_handler`. The handler currently handles a BLE-data
  command as a placeholder, but removing it still requires an explicit project
  decision. `controller_if/CMakeLists.txt` has its VNET guard commented out.
  Setting `WIFI_ENABLE=n` alone cannot demonstrate exclusion of this subsystem.
- Preserve `tuya_ipc_init()` independently: `tuya_tkl_ipc.c` dispatches HCI and
  SYS messages through `tkl_ipc_init`; it is distinct from the CIF customer
  handler. Preserve CP boot of the AP, heartbeat supervision, general mailbox,
  PM/flash/ADC services and Bluetooth IPC. Do not remove all mailbox code with
  VNET. No service ABI qualification of an off variant has occurred.
- The AP PSRAM layout has since gained a dedicated display DMA reservation at
  `0x60024000`, size 16 KiB, in addition to display and SD metadata regions.
  Preserve it and the CP heap at `0x60700000`; no Wi-Fi memory was reclaimed.
- Existing RF/latency hooks and manifest validation remain bound to the stock
  image and fixed function locations. A relink needs source-integrated fixes
  or newly verified exact-ELF hooks and variant-aware manifests. Do not bypass
  the current hash guards. A nominal AP option would leave the old CP active.

### CP build attempt and host validation

A separate copy of pinned T5AI was staged at
`/tmp/unhygienix-wifi-sdk-20261031`; the vendor submodule was not edited.
The CP-only command was `make bk7258_cp PROJECT=tuya_app
BUILD_DIR=/tmp/unhygienix-wifi-sdk-20261031/build APP_NAME=tuya_app
APP_VERSION=1.0.0 -j2`, with Python 3.12/project site-packages and
`TUYA_TOOLCHAIN_PATH` pointing to the available ARM GCC 14.2 toolchain.
Partition/region generation succeeded, but Armino stopped before configuration
or compilation because `cmake` is unavailable. The host check also found no
CMake executable. A first invocation's Python import error was resolved by
selecting Python 3.12 explicitly; the retained final log is `cp-on-build.log`.
This was a tooling probe, not stock reproduction or an off-variant link test.

The SDK's `toolchain_get.sh` and BK7258 `soc_config.mk` require GCC
10.3-2021.10. That compiler was not found in the inspected host/tool directories;
the available 14.2 compiler is sufficient for existing hooks but does not prove
reproducible vendor CP output. The SDK pins the Linux x86-64 compiler archive to
SHA-256 `97dbb4f019ad1650b732faffcc881689cedc14e2b7ee863d390e0a41ef16c9a3`.
Next: provision CMake and that verified toolchain into an isolated build
environment, reproduce the stock-on CP with the archived effective config, then
resolve the project/CIF dependencies above and build off/on profiles. Preserve
partition/shared-memory geometry and perform the variant packaging and ABI checks
listed in the original integration steps before changing the board default.

The unchanged stock-derived RF/latency hook build and CP manifest validation
passed. Rechecking the archived AP map passed; this is not a fresh AP build.
The same 70 host checks were rerun: **68 passed, one failed and one errored**.
`test_board_startup_elects_clock` uses a fixture for the old framebuffer-range
helper; current board startup uses linker range symbols and
`prv_validate_psram_range`. `test_unhygienix_pairing_uses_its_two_present_buttons`
expects old button declarations, now replaced by `PBL_GPIO_KEYS_DEFINE`.
Both reproduce on entry source without Wi-Fi edits; no unrelated driver/test
changes were made. Do not carry forward the earlier 70/70 claim to this HEAD.
No fresh AP or full CP image build passed in this session.

### Hardware and power observation

Host enumeration now finds FNB58 `2e3c:5558` as well as both CH342 UARTs.
The existing CRC-enabled logger received **1,976 samples**, then stopped cleanly
(exit 0). The recording ran 09:34:48–09:35:10 UTC / Europe/London on 31 October:
mean 4.98388 V, **29.486 mA**, **146.954 mW**, observed current 29.20–42.88 mA.
Raw samples, USB diagnostics and summary are retained. Operator reports USB
power, screen at lowest dimming and Bluetooth connected. Battery state of charge,
charging current, numeric brightness, display content and actual installed AP/CP
identities were not established. This is a bounded reading of that connected
setup, not a controlled firmware baseline or Wi-Fi saving. USB power includes
charger/regulator effects; nominal-sample energy integration and instrument
accuracy limitations from the tooling guide apply.

A separate six-second, read-only 460800-baud UART probe received 168 bytes on
`if00` and 24 on `if02`; both descriptors were closed and terminal settings
restored. Those bytes are not a decoded identity/boot log and were not used to
establish installed firmware or service health. No capture or soak remains running.

Evidence is at
[/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-optional-20261031/](/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-optional-20261031/).
Keep the earlier frozen baseline intact; it predates the latest recorded A5 AP
and must not be relabelled as today's installed image. The next power task is
to record actual firmware identities and charging/setup conditions for a controlled
baseline, then complete the CP build/variant gate above and compare matched
on/off images. If this gate remains blocked, explicitly retain that status before
moving to phase 2. No phase 2 or display power work was started.

## Evidence and identities

Production source inspected at `4d9976caa8b8cbc8f3cbfbc14c79c92b8649f86f`;
worktree initially clean. Applicable repository, board and workspace AGENTS.md
instructions and the power TODO/current-status pages were read.

Pinned inputs remain `boards/unhygienix/tuya/sdk.lock.json`:

- T5AI: `13379b63e07e78770fb4d0bffe36db2754658132`.
- Optional TuyaOpen: `e82840dc9b629cb77fa922fb04e3f820dc2e78b4`.
- Stock CP SHA-256:
  `0edde387b5694ac6687e04d318e4f7346acd29e585b699eba28ed70540d91856`.

The reference `.vendor/tuyaopen/platform/T5AI/t5_os/build/bk7258/tuya_app/bk7258/`
under the artifacts workspace contains the stock CP ELF, map, generated config
and binary. **Objcopy of that ELF exactly reproduces the hash-checked stock CP**.
The generated config therefore has substantially stronger provenance than a
project config fragment alone. Selected startup function bytes also match the
last recorded installed, fix-only CP (`1713dee17dd98cd8aea8114a438d24ed87112585c16c45960221c4b96f344efb`).
This verifies archived artifacts, not the device's currently installed identity.

All new evidence is preserved at
[/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-optional-20261030/](/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-optional-20261030/).
It includes `cp-evidence.json`, `cp-startup-disassembly.txt`, matching vendor ELF,
map/config, original build snapshots and SHA-256 manifests. Do not confuse the
old diagnostic `interval-fix/cp` recorder with the installed `fix-only/cp` image.

## Ownership trace

Vendor paths below are relative to
`third_party/hal_tuya/TuyaOpen-T5AI/t5_os/` unless stated otherwise.

| Area | Confirmed ownership and behavior |
| --- | --- |
| Native AP | `soc/t5ai/CMakeLists.txt`, `soc/t5ai/init.c`, board configuration and `drivers/t5ai/CMakeLists.txt` build native Pebble code, not the vendor AP RTOS, network stack or Tuya application. No Wi-Fi initialization or station/AP requests were found in the native board/SoC/drivers. Bluetooth uses `third_party/nimble/transport/t5ai_hci_ipc.c`. |
| Vendor AP reference | `projects/tuya_app/ap/config/bk7258_ap/config` enables `WIFI_TRANSFER`, `WIFI_VNET_CONTROLLER`, `WIFI_SOFTAP` and `ENABLE_LWIP`. These do not control the native AP or the already-built CP. The optional `boards/unhygienix/tuya` reference application is also a separate build. |
| CP build | Stock generated config has `WIFI_ENABLE=y`, `WIFI_VNET_CONTROLLER=y`, `WIFI_CP_IPC` unset, `BLUETOOTH=y`, `BTDM_CONTROLLER_ONLY=y`, `BLUETOOTH_SUPPORT_IPC=y`, `LWIP=y`, Wi-Fi CLI/AT enabled and `WIFI_PS_DISABLE` unset. Controller-only Bluetooth does **not** mean Wi-Fi-free CP. |
| CP startup | `cp/components/bk_init/bk_init.c:bk_init()` initializes components and PM mailbox, calls `app_wifi_init()`, then Bluetooth. Stock ELF has a direct call to `bk_wifi_init` at `0x0206b516`; it is not waiting for an AP request. |
| Wi-Fi initialization | `cp/components/bk_wifi/src/wifi_init.c:wifi_init()` initializes workqueue, votes MAC/PHY power and clocks, opens the Wi-Fi RF vote, initializes MAC/PHY and calibration, then calls `rwnx_intf_init`, `fhost_rxbuf_push`, `wpas_thread_start` and `cif_init`. ELF `wifi_init` at `0x0202d7d2` confirms these calls and votes. `bk_wifi_init` registers deep-sleep and low-voltage callbacks. Its success/init flag is not reliable evidence of all initialization succeeding: the wrapper ignores the underlying initialization result. |
| Tasks | The source starts the Wi-Fi workqueue, core/event and message handlers, supplicant and VNET controller thread. Generated config reserves core-task stack 2048 B and kmsg-task stack 4096 B; `cif_init` creates a 4096 B task plus queue and oneshot timer. Creation failures remain possible; these are configured/attempted allocations, not live measured stack usage. |
| Memory | CP linker `cp/middleware/soc/bk7258/bk7258_bsp.ld` retains Wi-Fi/controller/lwIP text and CP RAM/heap/PSRAM ownership. Wi-Fi task state, queues, protocol tables and VNET event buffers occupy static/dynamic CP memory. `os_malloc_wifi_buffer` uses the CP allocator on BK7258. `projects/tuya_app/partitions/bk7258/ram_regions.csv` reserves CP PSRAM heap at `0x60700000`, size `0x40000`. Native AP explicitly owns the first 128 KiB PSRAM plus its 8 KiB SD metadata reservation (`soc/t5ai/psram-memory.ld`). Do not reclaim vendor/AP media slabs or CP memory merely by disabling Wi-Fi. No exact recoverable-RAM total or RAM power saving has been established. |
| Power and clocks | Wi-Fi startup votes `WIFIP_MAC`, parent `PHY`, `PHY_WIFI`, MAC and PHY clocks, and `RF_OPEN/RF_BY_WIFI_BIT`. `bk_wifi_adapter.c` exposes later sleep, power and CPU-frequency callbacks to the prebuilt Wi-Fi library. Firmware inspection confirms startup votes, not their continuously elected state or idle duty cycle. Native AP's 240 MHz vote is independent of Wi-Fi ownership. |
| Shared RF/PHY | `app_phy_init` initializes PHY/RF adapters when either Wi-Fi **or Bluetooth** is enabled. `bk_pm/pm.c` tracks PHY_BT/PHY_WIFI/PHY_RF owners; Bluetooth's `soc/bk7236/bluetooth.c` uses its own PHY_BT and RF_BY_BLE votes. Keep Bluetooth, calibration, RF arbitration and PM support. Never gate the shared PHY/RF/PLL wholesale from AP. |
| Supervision/services | General mailbox, AP startup announcement, one-second CP heartbeat, flash, logging, ADC and PM IPC are independent service contracts. Keep `drivers/t5ai/cp_mailbox.c`, `soc/t5ai/init.c`, vendor mailbox/heartbeat and Bluetooth IPC. VNET's `cif_ipc_init` opens Wi-Fi channels; it is not a substitute for all mailbox services and its removal still needs a service dependency audit. |

## What is and is not established about power

Confirmed: the pinned executable contains an unconditional normal-boot Wi-Fi
initialization path and explicit power/clock/RF acquisition, even with no native
AP Wi-Fi user. The recorded installed CP retains that path. Wi-Fi being unused
does not imply it was never initialized or powered.

Not established: whether initialization completed on the current boot, present
MAC/PHY/RF vote values, automatic radio sleep, Wi-Fi task wake frequency or the
electrical cost of idle Wi-Fi. The library can subsequently change votes.
`wifi_deepsleep_enter_cb` releases Wi-Fi RF/PHY/MAC votes, but that callback is
part of the deep-sleep transition; it is not a supported live teardown API.
Native AP retained-sleep coordination is separate unfinished work. Do not invoke
that callback in isolation or infer steady Wi-Fi power from these source paths.

Host USB enumeration found CH342 UARTs `if00`/`if02` and no USB current-monitor
instrument. A bounded six-second read at 460800 baud received 0 B on ttyACM0 and
246 B on ttyACM1, saved with `hardware-read.json`. Both descriptors were closed
and original terminal attributes restored. No serial commands, resets, flashing
or running soak were involved. Received bytes establish a live UART read only;
they do not establish firmware identity or electrical state. No suitable current
measurement setup is available in this session; the operator is awaiting it.

## Runtime disable versus build exclusion

**Runtime full disable is unsupported by this pinned implementation.**
`wifi_deinit()` and `bk_wifi_deinit()` explicitly return `BK_ERR_NOT_SUPPORT`;
the latter even clears its init bit despite not freeing resources. Stopping a
station/AP or Wi-Fi RF vote alone does not delete tasks, free buffers, balance
every power/clock vote or unregister callbacks. No complete live shutdown and
restart protocol exists in the native AP mailbox interface. Adding one would
require CP ownership/state/error handling and qualification, not an AP-only flag.

**Build exclusion is a plausible vendor-supported configuration path, not yet
a qualified Unhygienix image.** `cp/components/bk_wifi/Kconfig` defines
`WIFI_ENABLE` (vendor default y), and its CMake omits Wi-Fi sources/libwifi when
disabled. Crucially, `cp/components/bk_phy/CMakeLists.txt` selects `libcom_phy.a`
(or its NSPE variant) for Bluetooth with Wi-Fi disabled. Both libraries exist in
the pinned BK7258 SDK. `bk_pm/pm.c` switches to
`phy_wakeup_for_bluetooth`; `bk_init.c` has a Bluetooth-only calibration branch
using `bk_cal_if_init`; stock generated config also sets `SOC_BK7236XX=y`,
so that branch applies to this BK7258 configuration. The stock ELF has no retained
`bk_cal_if_init` symbol. Simply bypassing its Wi-Fi call would not reproduce this
link/configuration/calibration change.

Additional integration remains: `controller_if/CMakeLists.txt` has its Wi-Fi
guard commented out and unconditional Wi-Fi/lwIP dependencies. CLI/AT, Wi-Fi PM
event integration and BLE/Wi-Fi exchange settings must be resolved together;
turning off just one symbol is not proven to link. No claim is made that a full
Wi-Fi-off CP build was attempted or that the vendor path already passes.

## Exact implementation dependency and next step

The blocking dependency is a **reproducible, packaged BK7258 CP variant with
Bluetooth-only PHY/calibration and the existing AP service ABI**.
`boards/unhygienix/firmware.cmake:t5ai_cp` currently calls
`tools/unhygienix_cp.py` on a separately supplied stock binary.
`tools/patch_unhygienix_cp_rf.py` checks the entire stock hash and fixed reset
function; the latency patch also binds fixed vendor functions/call sites. A full
CP relink moves those functions and cannot be accepted by those guards or fixed
addresses. Do not relax the hash or relabel a manifest to make it pass.

The concrete next Wi-Fi implementation task is:

1. Stage a pristine copy of pinned T5AI outside its submodule. Preserve the
   current `tuya_app` partitions, shared-memory addresses and service settings.
   First reproduce the stock Wi-Fi-on CP using the recorded generated config
   and appropriate vendor toolchain; retain compiler identity, map and config.
2. Add a board-owned CP build profile with `WIFI_ENABLE=n`, Bluetooth controller
   and IPC retained, and dependent VNET/CLI/AT/lwIP/exchange features resolved.
   Check actual selected PHY library and calibration/wake linkage. Keep an
   explicit Wi-Fi-on profile; default the future Unhygienix CP selector to off.
   Do not claim that a native AP Kconfig option controls the existing stock CP.
3. Integrate the existing RF `0x0202` and interval-transition corrections at
   source/link time (or regenerate and verify hooks against each exact new ELF).
   Define variant-specific manifests binding compiler, effective config, input
   libraries, ABI, layout and output hashes. Extend packaging/runner rejection
   tests. Preserve the stock rollback path and never accept an arbitrary CP.
4. Build both variants, inspect symbols/maps and exclusions, then verify boot,
   heartbeat, HCI reset, advertising, bonded reconnect, sustained transfers,
   25 reconnects, cold boot and overnight idle. Exercise flash/PFS, ADC,
   microphone and SD services without changing their power policies. Confirm
   per-owner RF/PM state with bounded diagnostics. Wi-Fi-on coexistence also
   needs qualification; it is not qualified by the current Bluetooth RF hook.
5. Measure matched idle/workload energy against the frozen baseline before
   marking electrical qualification complete. Do not combine this with other
   power changes.

This is a build/ABI integration blocker, not evidence that BK7258 cannot run
Bluetooth without Wi-Fi or that a needed PHY library is missing.

## Baseline preservation and validation

`artifacts/wifi-optional-20261030/baseline/manifest.json` binds 117 copied files
(320,942,623 bytes). Original build directories remain available and untouched.
The snapshot separates existing local builds (some old) from the last recorded
installed AP/CP. In particular, `build-main-tuya` is dated October 19 and must
not be labelled the October 22 installed AP. The installed-record AP build ID is
`c0e283f0b861e0d0fbcb70fb9c5fae6e86b84065`, raw SHA-256
`3320c182e51a5e886c844d1fe7db59409dd3b5b0d4a29c55071dff9274c2cc4b`.
Its protected image, ELF/map, dictionary and October 22 validation record were
copied. Installed-record CP, supported stock-derived CP and pinned stock rollback
input are separate directories. These identities were not re-read from flash.

The unchanged production CP source hooks were compiled and packaged from the
verified stock input into `repackaged-cp/`; manifest validation passed. The
recorded-installed AP map passed `check_unhygienix_image_map.py`. This is a CP
hook build and AP artifact check, not a full Wi-Fi-off CP or new AP build.
All 70 focused host regressions passed (CP manifest/RF/latency, image/packaging,
runner, power and board configuration); see `regression-final.log`. These do
not qualify Wi-Fi-off operation or electrical savings.
The initial test invocation used an interpreter without the project imports;
the corrected invocation supplies Python 3.12 site-packages and `pbl-cli` paths.

## Handoff when the current monitor arrives

Before changing firmware, verify the frozen-file hashes, re-establish actual
installed AP/CP identities, record fitted cell/supply voltage, instrument,
connection and sampling setup, phone state and fixed display content/brightness.
Measure connected idle and disconnected advertising first; preserve raw traces
and sparse diagnostics. Use the same setup for Wi-Fi-off/on candidates and
re-test the baseline to catch drift. Do not overwrite the frozen baseline with
newly built firmware. A deliberate rollback may require the documented
`--allow-older-version` AP path and CP-only writer for the archived installed CP;
historical installed CP is not a current normal-runner manifest. Validate exact
write ranges and preserve PFS/bonds.

The next power-optimization task is baseline measurement and completion of this
CP build integration gate. If CP integration remains blocked, explicitly retain
that blocker before moving to phase 2's balanced I2C clock work in
[power-todo.md](power-todo.md). No other power work was started here; display
power remains last.
