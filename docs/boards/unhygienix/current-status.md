# Unhygienix current status — 2026-10-30

This page supersedes installed-image claims and task dispositions in older dated
handoffs. Dates refer to Europe/London. Historical captures and failed candidates
remain evidence; completion by operator decision does not imply new measurements.

## Latest source and recorded device state — 2026-10-30

Production HEAD is `bf10c6435930c7d0f12ab335faf67c29f1d14b0c` (A4 committed),
following A3 `06079008a`, A2 `f7f7cbd84` and A1 `f3ebbee4c`. A5 remaining local
corrections are implemented and host/image validated in the uncommitted worktree.
The normal configuration was rebuilt and flashed AP-only at the operator's request.

**Latest verified installed AP:** ELF build ID
`901c6c58f0d520c5388b15a0865d4b21c055efb3`, normal A5, boot verified at
16:34:37.008 UTC on 30 October. Phone version:
`v4.41.2-670-gbf10c6435-dirty`. The guarded writer reports Flash OK (32.2 s).
UART confirms four-bit SD, the existing 32 MiB volume (`new=0`), 49 apps,
and encrypted bonded phone reconnection with PPoGATT reopened. CP, resources,
PFS and bonds were preserved; no provisioning or storage-backend switch occurred.

The 119.161-second flash/startup capture received 5,352 bytes and is stopped.
No fatal fault or uncommanded additional boot was observed. One reboot warning
corresponds to the commanded flash reboot. `Animation 3 does not exist` was logged
at embedded time 16:35:26.097 UTC; its cause remains unassigned and it is not
classified as fixed. See the [A5 flash record](/home/baz/Documents/ChatGPT/pebble/artifacts/a5-flash-20261030/RESULTS.md).
Startup/reconnection does not qualify eFuse faults, idle control, concurrent
microphone/display/SD DMA, storage integrity, cold boot or longer soak behavior.

The previous A4 installed ID was `943caf619e5bb3e49f6e852bfe97efe0eb8323f0`.
Its frozen AP/ELF/map/dictionary rollback inputs were hash-checked before A5 flashing.
[A4's earlier boot/reset/reconnection checks](/home/baz/Documents/ChatGPT/pebble/artifacts/a4-flash-20261030/RESULTS.md)
remain historical evidence, including current boot time after one controlled
hardware reset and a connection-parameter timeout recovered 412 ms later.
A4 console clock writes did not execute: the CP-owned runtime UART input backend
is unconditionally stubbed, unlike shared normal/MFG input on other SoCs.
Console integration remains separate work; do not reclaim AP UART0 pins from
four-bit SD. A3's earlier installed ID was `4245c7ab3e16d7c5449b864b355ea1ba22550993`.
All prior captures are stopped; A3/A4 qualification limits remain open.

[A1 is complete and explicitly accepted by the operator](/home/baz/Documents/ChatGPT/pebble/artifacts/a1-sd-reflash-20261030/RESULTS.md).
The previously accepted A1 SD AP had ELF build ID
`d316ed7087345e709826be1605b727bc9d0b1bc6`, flashed AP-only earlier on 30 October.
UART confirmed four-bit SD-backed PFS, the existing 32 MiB volume (`new=0`),
48 apps and an encrypted bonded phone session. Terminal watchface transfer
succeeded; the operator confirmed “all looks good”. Capture stopped. A recorded
first-launch app fault remains unresolved; 25-cycle, cold-power and overnight
qualification were not claimed.

[A2 F02/F06 is implemented, host validated and image validated](/home/baz/Documents/ChatGPT/pebble/artifacts/a2-mailbox-20261030/RESULTS.md)
and is now committed as `f7f7cbd84`. HCI queue-exhaustion diagnostics are bounded
and reported by the HCI worker; mailbox-owner logging and blocking APIs reject
before shared log-state/PULSE or transport locks. PM quarantine is checked under
the transaction mutex before draining responses or publishing another request.
Both failures reproduced against the entry source and pass corrected scheduling
regressions with real contending host threads/locks. Validation: 9 focused tests;
231 port tests plus 75 subtests; 28 shared tests plus 119 subtests. Three optional
CP execution tests and one shared ARM execution test remain unconfigured.
Fresh normal/release images passed packaging, map, resource, log-hash and syscall
checks; linked LTO paths retain the early owner guard. A2 was not flashed or
device qualified in its implementation session; its corrections are included in
the later A3 image. Its validation record describes the pre-commit source state;
the commit above records the subsequent source disposition.

SD PFS, four-bit mode and the existing-volume guard remain enabled by default.
A2's normal effective configuration exactly matches accepted A1; release retains
its existing PULSE default. CP inputs/wire ABI, Gabbro app/companion compatibility
and the one-second heartbeat are unchanged. No provisioning, NOR switch or new
CP image was performed.

[A3 / F03 is implemented, host/image validated and AP flashed](/home/baz/Documents/ChatGPT/pebble/artifacts/a3-hci-pressure-20261030/RESULTS.md).
Receive pressure now retains AP copies in the existing 1,285-byte staging budget
and retries nonblockingly, with explicit recovery on bounded exhaustion/deadline.
The duplicate payload queue was removed: normal has zero net SRAM increase over
A2 (20,192-byte linked heap arena); release adds 32 bytes. Validation: 233 port
tests plus 81 subtests; 28 shared tests plus 119 subtests. Both fresh images pass
protected packaging/maps/config checks. CP ABI, A1/A2 protections and SD defaults
remain unchanged. Runtime pressure/heap/stack and longer device qualification
remain pending; no operator acceptance or resolution of historical app/radio
faults is inferred from this flash.

[A4 / F04 and F05 is implemented, host/image validated and AP flashed](/home/baz/Documents/ChatGPT/pebble/artifacts/a4-rtc-20261030/RESULTS.md).
External STOP/write failures now attempt restart and retain the first error;
readback gates persistence health. Saved-mask tuple publication and ordered
cache-durable retained commits preserve the 136-byte layout and checksum.
Validation: 5 focused tests plus 74 subtests, 231 port tests plus 147 subtests,
and 28 shared tests plus 119 subtests; four existing optional execution checks
remain unconfigured. Normal/release images pass protected packaging/maps.
Committed as `bf10c6435`; its flashed normal image is now superseded by A5.
Device validation is limited to startup, normal reset/current boot time and
bonded reconnection;
console clock updates, actual RTC bus faults and interrupted resets remain open.

[A5 / F07–F09 is implemented, host/image validated and AP flashed](/home/baz/Documents/ChatGPT/pebble/artifacts/a5-local-20261030/RESULTS.md).
eFuse polling no longer underflows and requires operation completion before
READ_VALID data is accepted. T5AI honors the common idle gate while preserving
scheduler confirmation, elapsed accounting and tick behavior. A local GDMA owner
keeps display channel 0, microphone channel 1 and SD channels 2/3 routing independent
of microphone teardown, preserving inherited routing and CP ownership.
Separate F08/F09/F07 patches pass standalone regressions against entry HEAD.
Validation: 10 focused tests plus 8 subtests; 235 port tests plus 155 subtests;
28 shared tests plus 119 subtests. The same four optional execution checks remain
unconfigured. All nine fresh images (normal/release and all eight DMA combinations)
pass packaging/maps; normal/release configurations match A4 byte-for-byte.
Each adds 128 B AP flash over A4 with unchanged linked heap arenas (20,160 B normal,
29,216 B release). Shared code, ABI, geometry and qualified display polling/power
policy are unchanged; experimental IRQ/worker defaults remain disabled.
A5 is uncommitted. Physical eFuse/idle/mixed-DMA qualification remains open.

Power and architectural work remain later packages under the
[handoff](/home/baz/Documents/ChatGPT/pebble/audits/2026-10-30-tuya-port-review/HANDOFF.md).
The standing power sequence remains non-display first and display power last;
these correctness prerequisites do not claim electrical savings.

## Historical SD heap qualification — 2026-10-22

Production HEAD at the 22 October refresh was `87788c85a`. Recent committed changes are
`d4c9c5321` (display optimisation), `1428d0cb9` (SD PFS), `8ce938615`
(SD interrupt-race correction), `c79352159` (CP latency-transition fix), and
`87788c85a` (PFS metadata PSRAM allocation).

AP build `c0e283f0b861e0d0fbcb70fb9c5fae6e86b84065` was flashed AP-only on
22 October. Boot UART and a later live read verified its identity. The watch uses
32 MiB four-bit DMA SD-backed PFS; the existing volume reopened (`new=0`) with
48 apps. Internal-flash PFS remains preserved and independent. No CP change was
included in this AP correction. Capture was stopped and files flushed; this page
is a recorded state, not a claim of live monitoring.

The notification reboot was diagnosed as kernel-heap exhaustion/fragmentation.
The 8,192-byte PFS page-flags cache now uses a dedicated AP-owned PSRAM reservation
at `0x60020000..0x60022000`, outside the first 128 KiB display/board reservation.
Kernel heap capacity remains 21,216 bytes; final observed free space was 11,992 B,
largest block 9,948 B and peak used 13,540 B (7,676 B remaining at that peak).
These are unpaused live heap walks with concurrency/cache limitations, not
universal worst-case headroom measurements. Shared PFS and allocator source are
unchanged. Seventeen focused checks and image/build validation passed.

After an initial USB capture gap, 25.56 minutes of continuous UART recorded no
additional boot, fatal OOM, SD I/O error or watchdog report. Notifications displayed
normally. Seven supervision-timeout disconnects (`0x208`) all reconnected and
reopened the phone session. An optional image needing 12,508 contiguous bytes
was safely dropped when the largest block was 7,728 B. Radio stability and image
allocation limits remain open; this bounded run does not establish release stability.

Evidence: [heap-fix report](/home/baz/Documents/ChatGPT/pebble/artifacts/sd-pfs-heap-fix-20261022/results.txt),
[SD backend](sd-pfs.md), [tooling](tooling.md#sd-pfs-metadata-heap-fix--2026-10-22).

## Wi-Fi power investigation — 2026-10-30

[Ownership and implementation findings](wifi-power.md): the native AP contains
no Wi-Fi stack, but the verified pinned CP initializes Wi-Fi at boot and requests
MAC/PHY power, clocks and RF ownership. Steady-state votes/current remain
unmeasured. Runtime full deinit returns unsupported. A vendor Bluetooth-only
build path exists, but requires a reproducible CP build and variant-aware
packaging/calibration/ABI validation before a supported default-off option can
be delivered. Implementation is blocked at that integration gate; hardware and
electrical qualification are separately pending. No firmware behavior changed
or flashing occurred in this investigation, and no other power work was started.

Existing builds and the last recorded installed AP/CP were copied and hash-checked
in `/home/baz/Documents/ChatGPT/pebble/artifacts/wifi-optional-20261030/baseline/`
for benchmarking when the current monitor arrives. The old build directories are
not all the recorded installed build; use the snapshot identities. Seventy host
regressions, unchanged CP source-hook build/manifest validation and the preserved
AP map check passed. Host UARTs were enumerated; a six-second runtime read
received 246 B and then stopped. This did not re-establish installed identities,
measure current or qualify a new firmware image. See the report for the next CP
integration task and baseline measurement instructions.

## Completed work and retained configuration

- **Language packs: complete, closed by operator direction on 22 October.**
  Installation, replacement/removal, language selection and optional-font/glyph
  follow-ups are no longer TODOs. This disposition adds no new device-test claims.
  Keep the October 18 language-switch watchdog trace as historical evidence under
  general watchdog/settings-sync stability; it is not a language-pack blocker.
  English/base-font fallback and stock PFS watch/unwatch routing are implemented.
- Full system-bank payload CRC and table bounds/ID validation are implemented;
  recorded validation is 2,653 ms at 240 MHz. Explicit absence of an update bank
  is represented and unsupported updates remain rejected. Sharing the remaining
  bank backend with stock is separate cleanup.
- The October 19 display pass implemented wide copy/fill, native compositor
  copy/clear, two-row DMA batches, touch-move coalescing/acquisition timestamps,
  DMA rotation and elapsed-time deadlines/bounded recovery. Matched native
  compositor time fell from 6.671 to 1.887 ms/call. Eighty-two host checks passed
  and the final normal display image passed bounded operator checks.
  IRQ completion and display worker experiments remain disabled; IRQ was slower
  at this batch size (~26 versus ~20 ms/update). Pixel fast path, text cache,
  resource-wide copying, profiling and fault injection remain off in the selected
  normal baseline. Scalar rotated fallback was not qualified. DMA2D remains
  deferred without a hardware speed benefit. See the
  [display handoff](/home/baz/Documents/ChatGPT/pebble/artifacts/t5-display-implementation-20261019/HANDOFF.md).
- The October 21 CP interval-transition correction clears stale latency state at
  both parameter-application sites while preserving normal peripheral latency.
  Two Pebblecraft transfers passed, including latency 30 to 0. The recorder and
  zero-latency workaround were removed from production and archived. Later radio
  timeouts show this is not complete Bluetooth stability qualification.
  See the [CP notes](../../../boards/unhygienix/tuya/cp/README.md).
- Touch, battery and heartbeat private-worker consolidation, native kernel objects,
  retained boot bits/reboot records, stock reconnect policy and splash integration
  are implemented. Do not repeat these as new implementation tasks.

## Remaining work

Priority update — 2026-10-30: [power optimization](power-todo.md) is the
highest-priority power workstream, following the local correctness packages
recorded above. Complete its non-display phases first and leave all
display power work until its final phase. This changes work ordering only; the
historical measurements above are unchanged; the latest installed state is
recorded in the 30 October section.

1. Complete the [power TODO](power-todo.md), including measured qualification.
2. Diagnose remaining Bluetooth supervision timeouts; qualify sustained phone/app
   transfers, 25 reconnect cycles, cold-boot bond/face restoration and overnight
   connected-idle operation with periodic interaction.
3. Resolve general App/KernelBG watchdog and settings-full-sync stalls. Preserve
   prior first-failure evidence separately from the fixed notification heap OOM.
4. Benchmark SD throughput, CPU cost and tail latency; qualify full-volume,
   interrupted-write/corruption recovery and display/microphone concurrency.
5. Measure startup/peak heap and stack high-water under representative loads;
   address optional contiguous-image allocation limits. A whole-heap PSRAM move
   is not implemented; benchmark allocation/free/reallocation under fragmentation
   and normal display/Bluetooth load before deciding it.
6. Finish low-power/tick suppression, hardware wake/alarm routing, RTC drift,
   privilege/isolation and fitted-cell battery/charging qualification. Further
   accelerometer CPU-cost reduction remains exploratory; do not infer an
   exclusive pipeline baseline from instrumented task CPU figures.
7. Refresh the stock-convergence audit against current source/upstream; finish
   memory/capability/image-geometry contracts and shared-backend consolidation.
   Review shared GATT, PULSE, logging, pairing, serial, MPU and watchdog fixes
   independently for upstream. Complete relevant stock-board/QEMU checks.
8. Define firmware packaging, rollback, interruption recovery and factory restore
   before enabling OTA/recovery. Existing update gates remain in force.

SDK compatibility-cache removal/benchmarking, pixel implementation removal and
app-cache reserve/admission-policy changes remain deferred by operator direction.
The former internal-flash PFS expansion deferral does not undo the implemented
optional SD volume. Flash bypass remains paused. Language-pack work is closed.
No merge/release readiness is established by the short development captures.
