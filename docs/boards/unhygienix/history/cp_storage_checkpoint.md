# Unhygienix CP and storage checkpoint — 2026-10-03

> Current project status (22 October 2026): [implementation, completed work and remaining tasks](../current-status.md). Dated results below retain their original evidence limits.

This retained report records dated observations, not current operating instructions.
Use the [operator handoff](../index.md) and [tooling guide](../tooling.md)
for current procedures. Obsolete commands and machine-specific paths have been
omitted. Artifact labels are descriptive; external captures are not distributed
with this repository. Recorded dates, measurements and SHA-256 values are retained.


This report records the CP, flash, PFS, Bluetooth and validation state observed on the stated date. Use the current operator handoff for operating instructions.

## Current interpretation and implementation inventory — 2026-10-03

The accepted clean-image transfer/reconnect regression below is an earlier
checkpoint. Subsequent sections record supervision loss and an App privileged-code
stall/reset on diagnostic firmware; root causes and reliable Bluetooth recovery
remain unresolved. The later stable interval does not establish a fix. Image and
capture states are dated observations and must be checked on hardware before
claiming current monitoring.

The [implementation inventory](../index.md#implementation-gaps)
separates actual placeholders (WFI-only sleep, missing RTC wake alarm, console
input, flash logging, boot-version reporting and unprovisioned OTP) from disabled
capabilities (persistent coredumps, audio/absent peripherals and unsafe updates)
and validation gaps. Native watchdog and primary peripheral drivers are
implemented; phone identity reports Round 2. Cold-boot persistence, soak,
battery/power and interrupted-storage recovery remain open. Preserve resource
warnings and first-failure evidence even when faces load successfully.

## Final firmware and accepted validation

The watch runs the clean build `v4.36.0-190-g606b3781b-dirty`. Commit `606b3781b` contains the CP/storage changes; the subsequent instrumentation cleanup was uncommitted when flashed. The final AP-only flash completed in 31.2 seconds over physical `0x132000..0x2ca94c` (end exclusive). Raw image: 1,575,096 bytes, SHA-256 `33aa1869a641627af14c6260edcff252e0c1fe33cdd7e3533064748924b06559`. Protected image: 1,673,548 bytes, SHA-256 `12a7b49a49b22760690256a3bb0aa71c606e97d95b9d7fd1fa934893eb1ec440`. Startup reached runlevel 4 and “Ready for communication”; its bounded capture is complete and stopped.

Before removing the diagnostic instrumentation, six AppFetch transfers completed with result 0 and the operator confirmed every tested face loaded correctly. Five disconnect/reconnect cycles each restored encryption and a phone protocol session. No additional AP/CP reboot, assertion, mailbox CRC error, completion error, or measured flash-read failure appeared in that run. The operator accepted this coverage as sufficient for now and explicitly deferred the proposed 25-cycle test. This is short-run acceptance, not an overnight soak result. The cleaned build has startup coverage; the six-face and five-cycle regression was on the instrumented build with the same functional fixes and cache setting.

The cleanup passed 85 relevant host tests and a Unhygienix build. Earlier fix/packaging validation passed 92 focused tests. The compiled flash backend test exercises both header-cache settings, NOR delete masks, write/erase invalidation and the last expanded filesystem page.

## CP transport corrections

- `src/fw/drivers/t5ai/cp_mailbox.c`: a heartbeat completes on physical ACK state 2; state 1 remains pending for a separate control response. CP does not reliably send a second logical response after an already-complete physical ACK. The serialized heartbeat task keeps periodic control traffic independent of application activity.
- `src/fw/drivers/t5ai/mipc.c`: CRC8 now matches the pinned CP's reflected, right-shifting polynomial `0xab`, replacing the AP's MSB-first `0xf7` implementation. Host regression checks all 65,536 seed/byte transitions.
- `src/fw/drivers/t5ai/mailbox_channel.c`: serialize ordinary AP commands across logical channels. CP's rotating shared command/ACK adapter buffers cannot safely support the previous independent per-channel physical sends. Physical ACKs bypass this gate; round-robin selection prevents one channel monopolizing it.
- Drain received frames and queued ACKs before publishing ordinary commands or declaring a timeout. Defer timeout expiry while receive work remains. Bootstrap handles interleaved incoming commands and ACKs, and rejects unregistered commands instead of claiming success. Respect the synchronous-transmit/no-ACK contract.
- Preserve a completed synchronous transaction descriptor until its caller copies the ACK, and serialize lazy synchronization-object creation. These changes prevent descriptor reuse and concurrent initialization races.
- Whole-chip timeout recovery and SoC-local AP reboot-reason retention remain functional code. An AP-only reset may leave CP radio/mailbox state wedged; full power cycling is still the recovery for that condition.

Static comparison confirmed the installed CP image matches the pinned vendor binary. Raw CP reset text is hexadecimal: `reset reason: 10` means `0x10`, the NMI watchdog reason, not decimal 10/data abort. Preserve raw UART bytes when diagnosing CP failure; decoded PULSE logging omits vendor text.

## Flash correctness and performance

`src/fw/drivers/flash/t5ai_flash_ipc.c` uses a 256-entry reflected CRC32 table with seed zero and no final XOR, matching CP. The shared zlib-style CRC variant is unsuitable. Regression compares lengths 0 through 2,048 and the `123456789` result `0x2dfd2d88`.

The 31-second routine AP-only flash writes only the exact protected firmware
length rather than provisioning resources and an empty filesystem on every
update. Runtime CRC speedups are separate from programmer throughput.

The client priority boost covers the complete flash read-buffer lease: request, response, cache invalidation, copy/CRC validation, and READ_DONE. Priority is restored on all exits. This remains driver-local; shared Pebble task priorities and common timer code were not changed for these fixes.

Unhygienix skips the extra PFS read-consensus validation and repair retry. Version verification reports the first mismatch as an error instead of rereading/reprogramming it to obscure the failure. Other board configurations retain their existing path. Mutating flash operations are not blindly retried after an ambiguous timeout.

The cache-disabled diagnostic run measured 18,176 CP reads with zero failures, mean 2.09 ms and maximum 12 ms, with a 72.3% general-cache hit rate across 64,512 completed read chunks. These include startup and transfer work, not a controlled A/B benchmark. Full-length misses now read 512-byte lines; they cannot be compared directly with the earlier mix of 32-byte header reads.

## PFS corruption and recovery

The 8 MiB flash inventory found 24 live files occupying 51 pages and 202,131 declared payload bytes. Their headers and chains validated, without crosslinks or orphaned live continuation pages. There were 43 normally deleted pages, 160 pages with invalid flags `0xfd`, and two unformatted pages. The original 256-page region had no erased-format free pages.

PFS has a 28-byte page header; the old driver cache stored 32 bytes per page.
The 16-byte file header and 32-byte metadata follow it. Flags are active-low;
CRC validation must normalize mutable last-written state as the implementation
does, rather than treating the cache size as the serialized header size.

The driver updated its per-page header cache with memcpy although NOR programming clears bits by AND. A deletion writes mask `0xfd`: a start header must change `0xfa & 0xfd = 0xf8`, and a continuation must change `0xf6 & 0xfd = 0xf4`. The cache incorrectly held `0xfd`; PFS then treated those pages as occupied. Reusing a cached header during GC is consistent with the durable corrupted flags in the snapshot, but that propagation was inferred rather than directly captured. Changing each poisoned flag back to exactly one of the original start/continuation types restored its stored page CRC. All 61 former starts also had valid file-header CRCs and completed deletion metadata.

The cache update now models NOR with bytewise AND. A fresh pre-write backup was taken. Migration repaired only the 160 CRC-identified deleted flags and initialized the added pages; all other bytes in the original PFS were preserved. Restored deleted flags make 640 KiB eligible for GC; they do not imply 640 KiB has already been erased. The complete expanded readback matched the candidate byte for byte, SHA-256 `3d866d33e33e1d6cac87670fe924c7771c986a6b385a2307c6039f3dfde16beb`.

Appglancedb error `-6` was out-of-storage despite successful CP reads. The invalid occupied pages explain why the allocator ran out before the apparent live payload filled the old partition. Increasing capacity alone would not have fixed that cache bug.

## Expanded storage contract

All ranges below are physical flash offsets, end exclusive. PFS is raw CP-flash data; protected AP/resources use the BK7258 34:32 transform.

| Region | Range | Current treatment |
| --- | --- | --- |
| System resources | `0x2db000..0x3eb000` | Protected 1 MiB logical bank; preserved by AP-only flash |
| Shared PRF | `0x4ea000..0x4fa000` | Preserved |
| PRF/PFS gap | `0x4fa000..0x500000` | Preserved |
| PFS | `0x500000..0x7c0000` | 2.75 MiB, 704 pages of 4 KiB |
| End guard | `0x7c0000..0x7c9000` | Preserved |
| OTA manager | `0x7c9000..0x7cb000` | Preserved |
| User configuration | `0x7cb000..0x7cd000` | Preserved |
| Tuya configuration | `0x7cd000..0x7fe000` | Preserved |
| RF calibration | `0x7fe000..0x7ff000` | Preserved |
| Network configuration | `0x7ff000..0x800000` | Preserved |

The added `0x600000..0x7c0000` was completely erased in the inventory. It supplies 448 additional pages, 1.75 MiB. Firmware `FLASH_REGION_FILESYSTEM_END` is `0x027c0000`; packaging `PEBBLE_FILESYSTEM_SIZE` is `0x002c0000`. The expansion consumes part of the unused factory OTA-data allocation; OTA and general recovery remain unvalidated/disabled.

Routine updates must use `tools/flash_unhygienix_firmware.py --flash`, which writes only the exact protected firmware range and preserves resources/PFS. `pbl flash` provisions a new empty PFS image as well as AP/resources and therefore replaces existing filesystem contents. It is not the routine update command for this migrated watch. Never flash the unprotected logical `pebbleos.bin` directly or rerun the one-off migration against a changed filesystem.

## Header cache and diagnostic cleanup

`CONFIG_T5AI_PFS_HEADER_CACHE` remains available for A/B builds but is explicitly unset in the former `boards/getafix/defconfig.unhygienix` (now `boards/unhygienix/defconfig`). Both modes retain the four-line, 512-byte general read cache and write/erase invalidation. The per-page cache is an optimization, not a correctness requirement. If enabled, it remains bounded to the original 256 pages rather than growing with filesystem size.

Initially scaling the header cache to 704 pages consumed another 14,336 bytes plus valid-bit storage and caused an assertion allocating the PULSE task. That first-failure trace was preserved; bounding the cache fixed the startup failure. Disabling it subsequently reclaimed 8,224 bytes; the instrumented build netted 8,200 after its 24-byte counters. Removing those counters returns the remaining 24 bytes.

Temporary heap probes were removed earlier at the operator's request. Final cleanup removed CP-read RTC timings, cache counters/configuration, cache-mode boot logs, verbose retained-connection-policy logs, the AP/CP reset-source boot log and its unused CP getter. Existing mailbox failure diagnostics, HCI disconnect snapshots and normal error reporting predate this session and remain. No diagnostic capture is currently running.

The T5AI connection policy retains controller-negotiated link parameters and grants logical responsiveness requests locally; it does not initiate the unreliable CP peripheral-side L2CAP update. Observed links used 30 ms, zero latency, 5-second supervision. Do not describe the current implementation as enforcing 15 ms or 20-second supervision. Low-power parameter transitions and battery impact remain unvalidated.

## Evidence and remaining work

Artifacts are retained under `external artifact: logs`:

- `flash-inventory-2026-10-03.bin`, `.json`, `.md`, and `pfs-files-2026-10-03.csv`: full inventory and validation. Protected AP, CP and resource CRC16 blocks had no errors; decoded system resources matched the build pack.
- `pfs-pre-expansion-2026-10-03.bin`, `pfs-repaired-expanded-2026-10-03.bin`, `pfs-expanded-readback-2026-10-03.bin`, and `pfs-expansion-manifest-2026-10-03.json`: migration backup, candidate, exact readback and provenance.
- `pfs-expanded-runtime-2026-10-03.log`: first SRAM/PULSE allocation failure; `pfs-expanded-bounded-runtime-2026-10-03.log`: corrected startup/reconnection.
- `cp-cache-disabled-raw-2026-10-03.bin`, `cp-cache-disabled-decoded-2026-10-03.log`, `cp-cache-disabled-loghash-2026-10-03.json`, and `cp-cache-disabled-build-2026-10-03.json`: accepted transfer/reconnect regression and matching dictionary.
- `cp-clean-startup-2026-10-03.log`: final clean-firmware boot.

Cold-boot bond restoration and selected-face persistence, overnight connected-idle stability, and power behavior remain untested in this session. Glyph/resource-check and missing app-entry warnings remain despite operator-confirmed face loading; do not count the good transfer test as proof these warnings are resolved. Bidirectional serial console failed to establish, so runtime `pfs ls` is unavailable; capacity is established by source/image layout, flash readback and operation on the watch. Any future uncommanded AP reset remains a release blocker. Preserve the first failure and its raw bytes/dictionary.

## Bluetooth diagnosis update — 2026-10-03

The earlier cleaned-image startup checkpoint is superseded for the physical watch by an AP-only Bluetooth diagnostic image. Host hardware access outside the sandbox is explicitly authorized by the operator; use escalated host commands and verify received UART bytes. Source of truth remains this tree; Tuya reference sources were read without modification.

Current diagnostic AP raw SHA-256: `f8aa704ff36104e6470828f70744bf527d5fe857867e3d4f01260ab733ec5b4f`; protected: `d93cc268961262d3f6fbde7345fd4810c329a7cabcc553ddadce5ce7e918580e`. AP-only flash range `0x132000..0x2cabd2`. Matching artifacts, complete raw UART, decoded logs, source patch and first-failure prefixes are in `external artifact: logs` under `bt-flash-timing-*`. Build and 65 board/kernel tests passed.

The instrumented run reproduced supervision timeout at 16:50:15 after 449276 ms of protocol session. Flash IPC maxima: reads 5 ms, writes 9 ms, erases 6 ms; no operation active, last operation about 9.2 seconds earlier. A flash transaction spanning the final five-second supervision window does not fit this reproduction. Transport timeouts/exhaustion/fault remain clear. CP advertising event counts advance after failure, but RF transmission and connection-request acceptance have not been verified. Healthy and failed PM snapshots match. Scheduler priority warnings also occur before successful transfers, so they are not sufficient proof of root cause.

A deeper vendor Bluetooth reset diagnostic observation failed recovery and was removed from production source; its firmware/source artifacts are preserved separately. Flash timing diagnostics are T5AI-local, sampled atomically without per-operation UART output, and emitted at HCI heartbeats/disconnect. Fields are approximate concurrent observations. All captures in this checkpoint have stopped. Root cause and reliable recovery remain unresolved; next focus is bounded pre-failure CP connection-state sampling and on-air connection evidence.

### Subsequent CP-history diagnostic image


## AP crash and fault-context follow-up

At17:09:33.444 the CP-history run captured `ASSERTION at LR0x214b7c3`, `process_manager.c:106 *** CROAK: task stuck inside privileged code!`, followed by CP reset reason10 and successful reconnection at17:09:49–50. MatchingELF resolves LR0x214b7c2 to `prv_force_close_timer_callback`. The force-close callback finds the App task privileged and deliberately asserts. T5AI persistent core dumps are unavailable, so the initiating blocked operation was not captured. Operator identifies the open watchface as Professor Layton – Mini London. Watchface crash reports also occurred before the whole-chip reset. This establishes the immediate reboot cause, not its underlying blocked syscall or a causal link to radio supervision loss. Raw full capture `bt-cp-ring-raw-2026-10-03.bin`:45644bytes, SHA256e91e0da74db970ae9676420962c2f1ea5cfdac4b8c92f3a710429536e521c8a2; ended naturally.

The next AP-only image adds T5AI-local pre-reset reporting of App saved CONTROL/PC/LR and syscall TLS LR/SP, followed by CP history. ISR/critical/scheduler/reentry guards avoid unsafe inspection; shared process management is unchanged. Build,65board/kernel tests, sampler harness and fault-report guard harness passed. Raw AP SHA2565ef7967425831ca2ed4104a023ab784093f3d998ed0fd49dc7caf95502fd151c, protected057be5417161cf4522b53b56842e83f39636bc22b6afd3198ffbb28638945bdb, range0x132000..0x2cae36. Artifacts `bt-fault-context-artifacts-2026-10-03/`.

### New supervision-loss evidence

At17:23:10.358 this image reproduced reason0x208 after156966ms protocol session. CP ring shows successful-sync event5099/clock512791 last advanced atAPtick158615; subsequent scheduled events advance to5244 at162867 while synced remains5099. State alternates WAIT/ACTIVE, priority remains112, flags0x473 (event-sync bit0x100 clear). Thus the controller schedules connection events through the final approximately4.5seconds without successful synchronization. This does not fit continuous scheduler starvation with priority255. It does not prove RF transmission, reception, or correct channel/timing: review RF/connection-event configuration and obtain on-air evidence next. No active flash; lastreadtick153787, about9.4seconds before disconnect; maxread8ms/write11ms/erase5ms. Mailbox timeouts/exhaustion/fault remain clear. Advertising command succeeds. Frozen prefix `bt-fault-context-first-supervision-loss-2026-10-03.bin`.

Last captured heartbeat17:23:39. Subsequent diagnostic CLI `ble dump` and `help` produced no received bytes by17:25; AP heartbeat is also absent. Capture process remains open but cannot prove the watch remains alive. Operator display/button observation requested before reset. Preserve this distinction from the earlier continuously responsive CP failed-advertising case. Root cause and reliable recovery remain unresolved.

### Capture interruption correction

Host check at17:26 found runtimeUSB interface re-enumerated fromttyACM1 tottyACM2, with defaultbaud9600 on the new device while the capture retained its oldfd. Thus silence after17:23:39 cannot be attributed to a watch hang. Initial reopen encountered transientEBUSY; adapter re-enumerated again tottyACM1. Capture resumed at460800 in `bt-fault-context-resumed-raw-2026-10-03.bin`. Original prefix retained. The gap can contain unobserved events; do not join the two captures as continuous evidence. Operator is loading Professor Layton – Mini London again.

Resumed capture verified receivingPULSE andCPCLI bytes. HealthyCPdump at156937ms reports CON interval96/timeout16000/sync4637/RSSI−67;17:28:13ProfessorLaytoninit and subsequentwatchface switches completed. Lastchecked17:30:48connected heartbeat with no newassert. Resume is a freshboot tickdomain; startup occurred within the unobservedUSBgap, so rebootreason cannot be assigned from this segment. Pre-reset hook reporting remains enabled; `pulse_logging_log` sends inline in normalthreadcontext and guardavoidsqueuedISR/criticalpaths. Resumedcapture bounded720seconds from17:27; originalcapturestoppedafterUSBre-enumeration. Recordingcontinueddata is not proof the underlyingfault is fixed.

## Continued tracing requested17:33

Resumed shortcapture stopped for handoff: 22763bytes SHA2567d91945dd7223af9174e7171f6dc5938826b6cb7684c7b8fc08a4a61906fb30c. Continuous successor `bt-continued-2026-10-03-1735-raw.bin` began about17:36:07, using matchingfault-context dictionary. Briefhandoffgap possible; keepsegments separate. Newcapture reopens the stableUSBpath ondeviceinode/rdevchange orI/Oerror, configures460800baud, decodes every10seconds and freezes the firstdecodedLEdisconnect. It retains60secondsafterthat event, thenstops, orstopsatonehour (~18:36) ifnoevent. Script retained as `bt_continue_trace.py`. Physicalincomingbytes verified.

Operatorreportedlinkdrop duringhandoff/newcapture; bothprefixes frozen as `*-operator-drop.bin`. Currenttrace insteadshowswatchface740 crash/revert at17:36:24; noHCI LEdisconnect captured. CPsynced counts continue through17:38:39(event22894=synced22894), withphoneAppMessagesafterreportedincident. Useranswered “yes” toalternativeUIquestion, whichdoesnotdisambiguatephoneUI versuswatchface. Do notclassify thisreportedincident as a confirmedBLEtimeout. Actualfirst-radio-drop capture remainsarmed.

## Stability checkpoint17:53

Operatorreports unable to reproduce disconnect and stableoperation. Continuedcapture verifiedlive17:53:22, with freshUARTbytes and activeHCIheartbeat17:53:02. NoLEdisconnect, supervisiontimeout, privilegedtaskassert orfaultcontextreport in successorsegment. About17minutes of continuoussuccessorcapture, with priorresumedhealthyobservations beginning17:27. Watchface/appswitches andAppMessages continue. HCIqueue0/fault0; lifetimeflashmaxread12ms/write60ms/erase7ms. This is a stabilityobservation, not a verifiedrootcausefix: currentimageisdiagnostic, and earlierfailureoccurredonthesameimage. Capture remainsboundedtoabout18:36 orfirstdisconnectplus60seconds. Keepcurrentbuild/configuration forcontinuedcomparison ratherthanintroducinganotherchangebeforeafailure.
