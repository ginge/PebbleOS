# Unhygienix phone-sync incident: 2026-09-14 through 2026-09-15

> Current project status (22 October 2026): [implementation, completed work and remaining tasks](../current-status.md). Dated results below retain their original evidence limits.

This retained report records dated observations, not current operating instructions.
Use the [operator handoff](../index.md) and [tooling guide](../tooling.md)
for current procedures. Obsolete commands and machine-specific paths have been
omitted. Artifact labels are descriptive; external captures are not distributed
with this repository. Recorded dates, measurements and SHA-256 values are retained.


## Latest checkpoint — 2026-10-03

The [CP and storage checkpoint](cp_storage_checkpoint.md) records
implemented mailbox/CRC and NOR-cache fixes, 2.75 MiB PFS migration, six loaded
faces and five successful encrypted reconnects. Later diagnostic runs reproduced
supervision loss and an App privileged-code stall/reset. Root causes and reliable
Bluetooth recovery remain unresolved; a subsequent stable interval is not a fix.
The latest recorded image is diagnostic. Capture state is a dated observation,
not proof of current monitoring; verify device visibility and received bytes.
Cold-boot persistence, overnight soak and power behavior remain open.

See the [implementation-gap inventory](../index.md#implementation-gaps)
for explicit low-power/RTC-alarm, console-input, flash-logging, boot-version and
OTP placeholders, disabled coredumps/audio/optional hardware, and gated updates.
Native watchdog and primary peripheral paths are implemented; device identity
reports Round 2. Older image hashes and implementation claims below are historical.
Shared-code cleanup remains paused unless explicitly resumed.


## Historical paused checkpoint: end of 2026-09-15

This section records the September pause. Use the October checkpoint above
for current state; retain these notes as failure evidence.

The most recent phone sync failed before AppFetch, PutBytes, or any flash write
began. The trace contains only the reversed PPoG session opening and small
Pebble Protocol setup messages. Therefore slow NOR/CP flash writes did **not**
cause this particular failure. Earlier attempts did reach flash and exposed a
separate mailbox/flash failure, so storage-path reliability is still not
proven.

The watch is not currently visible as a USB serial device. No retained mailbox
snapshot could be collected after the latest failure, and a later full power
loss will clear retained RAM. The latest captured trace is:

```text
external artifact: t5ai-15ms-sync.log
```

It shows:

```text
PPoGATT Session is opened (reversed, Vers: 1 TXW: 4 RXW: 17) after 64ms
PP RX ep=0x0011 len=25 sn=0
PP RX ep=0x0010 len=1 sn=1
PP RX ep=0x1389 len=11 sn=2
PP RX ep=0x000b len=21 sn=3
PP RX ep=0xb2db len=3 sn=6
PP RX ep=0x0020 len=13 sn=7
PP RX ep=0xb2db len=4 sn=8
```

There is no AppFetch endpoint, flash transaction, assertion, AP reboot, or
disconnect record in that short capture. The app reported failure before the
trace was allowed to record the final phone/controller close reason.

### Fixes completed during this session

The Tuya/Beken physical mailbox acknowledgement contract was compared directly
with the reference implementation. The receive acknowledgement must overwrite
and return the same sender-owned command buffer. PebbleOS had been copying
high-rate acknowledgements into a small rotating ring, allowing an ACK buffer
to be reused before the CP consumed it. T5AI HCI, flash, console, control, and
reset-resynchronization paths now acknowledge in place. The low-rate SARADC
compatibility path remains separate because it rewrites the response address.

An initial physical-ACK guard also compared the returned parameter word. That
caused a boot loop because the CP legitimately replaces the first parameter
with its response-buffer address. Parameter equality was removed; the guard
now matches channel, sequence, and command only.

With same-buffer ACKs, a later sync stayed up without rebooting. Service
discovery took about 17.5 seconds, the phone unsubscribed after roughly 37
seconds, and then remotely terminated the HCI connection with reason `0x13`.
Mailbox counters remained healthy (`senderr=0`, `rxto=0`, `ackmis=0`, high-water
mark 4), and there was no new retained flash failure. Artifacts:

```text
external artifact: t5ai-inplace-ack-sync.log
external artifact: t5ai-inplace-ack-sync-mailbox.raw
```

Because Android had established the link at a 30 ms interval, the T5AI-local
active policy was then changed to request 15 ms, zero slave latency, and a
20-second supervision timeout. Low-power/nonzero-latency transitions remain
disabled. The latest sync capture did not include the connection-update event,
so it does not prove whether the CP accepted or applied 15 ms.

Review after that failure found a real link-policy state bug: if the controller
rejected the 15 ms request, `is_request_pending` remained set forever. The
current source clears pending state on both successful and failed completion,
allows the logical responsiveness request to proceed using the established
latency-zero link, and logs both request acceptance and the actual result:

```text
T5AI conn param request: current=... latency=... target=12 accepted=...
T5AI conn param result: status=... interval=... latency=... timeout=...
```

The HCI diagnostic also now prints the ATT handle and no longer labels every
Handle Value Notification as PPoG. The earlier `PPoG TX control type=6` line
was likely another GATT characteristic's payload, not proof of a corrupt PPoG
type; the next trace will distinguish this explicitly.

### Flashed image versus ready image

The image currently believed to be on the watch is the 15 ms request build:

- raw SHA-256
  `5c6a97ed94e47d51e0562b8349ad0bca5c615a8275e90d6b64bbcb2f47f9fa1d`;
- protected SHA-256
  `ff3106521a8ea4c0a2a2dc8192a23049f83d1426300842e3fb416df42c758f7e`;
- AP protected range `0x132000..0x215622`.

The pending-state fallback and improved logging were built after that attempt
but have **not** been flashed. Current build artifacts are:

- `build/pebbleos.bin`: 876,659 bytes, SHA-256
  `ef3174d7ef049fbd3da6be1bc5e752ff56d552de20034a1639c8f0fa62551649`;
- `build/pebbleos.t5ai-crc.bin`: 931,464 bytes, SHA-256
  `d83bf6a600f4c80e6f85bb61d628b915cb41578f22de1b1abd66fbef1782a4f6`;
- `build/pebbleos.t5ai-flash.bin`: 3,268,608 bytes, SHA-256
  `83bb0f18520293eaa93cfbec30cc63df242550364a0bee76ad11477251b2c6e5`.

The 57 focused Getafix/Tuya tests pass and `pbl build` succeeds. The full test
suite was not rerun; a previous full run hung in an existing PFS test and was
interrupted.

### Resume procedure

1. Replug the watch and confirm both dual-UART interfaces appear. A power
   cycle is now unavoidable, so the latest retained-RAM evidence is already
   considered unavailable.
2. Confirm a stable cold boot before writing anything. Do not start a phone
   sync on the currently flashed image.
3. Re-run the 57 focused tests if the working tree changed, then rebuild.
4. Flash only the guarded AP image using the documented Unhygienix AP-only
   procedure. Do not overwrite the bootloader, CP firmware, resources, bond
   data, or PFS.
5. Start a fixed-output trace with idle reconnect disabled before connecting
   the phone:

   Obsolete command sequence omitted; use the current tooling guide.

6. Connect the phone, wait for the `T5AI conn param result` line, and only then
   initiate one watchface sync. Let the trace continue through the final
   success, phone close, HCI termination, or AppFetch/flash failure.
7. If AppFetch begins or the AP resets, stop PULSE without removing power and
   query `ap_cmd PBLIPC` through the raw `if02` CP shell at 460800 baud. Save
   `PBLIPC`, `PBLIPCF`, and `PBLIPCFE` before any power cycle.

The next decision depends on the negotiated interval and failure stage. If the
15 ms update is rejected but setup completes, keep the fallback and investigate
phone timeout/session behavior. If it is accepted yet the phone still closes
before AppFetch, trace the PPoG/GATT session rather than flash. If AppFetch
starts and flash fails, return to the retained mailbox/flash snapshot.

## 2026-09-15 trace-ready checkpoint

After the required full USB power cycle, the watch booted normally and the
idle mailbox path was healthy. Temporary T5AI-local diagnostics now record a
32-entry in-RAM AP/CP mailbox flight recorder, sparse PPoG receive/transmit
sequence discontinuities, and a first-failure snapshot for flash IPC. The
failure snapshot includes the last eight mailbox events and lives in the
T5AI retained NOLOAD region, so an AP assertion/reset no longer destroys the
decisive pre-failure state. Query current and retained state through the CP
shell with `ap_cmd PBLIPC` on `if02` at 460800 baud.

The exact AP-only diagnostic artifact flashed for the next phone attempt is:

- protected image: `build/pebbleos.t5ai-crc.bin`, 931,226 bytes, physical
  range `0x132000..0x21559a`, SHA-256
  `648d5a4137bb8a2f5d31b758ab0c8e4963272261063abaa736660a181d3fa88d`;
- raw image: `build/pebbleos.bin`, 876,435 bytes, SHA-256
  `9c4285401c19f5127db754e6cf2c4b861392ad3356216861eba9485539ebc889`.

The guarded flash completed successfully without touching PFS, resources, CP
firmware, the bootloader, or shared bond storage. The post-flash baseline was
`senderr=0`, `rxto=0`, transmit depth `1`, and transmit high-water mark `3`,
with no retained failure record. A subsequent decoded trace ran for roughly
145 seconds but no phone ACL connection arrived; it contains only the known
NimBLE `BLE_HS_EALREADY` (`0x0002`) advertising retry noise. That capture is
`external artifact: t5ai-watchface-retained-trace.log`.

For the next attempt, start `tools/unhygienix_trace.py` with idle reconnect disabled,
connect from the phone, and initiate the watchface transfer. If the AP resets,
stop the PULSE decoder, then retrieve `ap_cmd PBLIPC` through a raw 460800-baud
CP-shell session before removing power. `PBLIPCF` and `PBLIPCFE` are the
retained failure summary and event lines. Do not power-cycle first, because a
full power loss clears retained RAM.

### 2026-09-15 failed watchface-sync capture

The next phone attempt reported that sync failed. The watch was left powered,
and `ap_cmd PBLIPC` successfully recovered the first retained failure:

```text
PBLIPCF ctx=464c0005 s=-1 t=16555879 tx=3693/7100/3691 ack=3408
         depth=2 high=4 rx=7098 ack=3691 route=429 unroute=176
         senderr=0 rxto=0 trace=22187
```

Context `0x464c0005` is the `FL` marker plus flash command 5, `WRITE`; status
`-1` is `E_ERROR`. This is not a physical mailbox-full or receive-timeout
failure: both diagnostic counters remained zero, the queue high-water mark was
only four of sixteen, and the AP remained alive and responsive afterward.

The retained events immediately preceding the failed write were:

```text
t=16555859 RX/route  h=47500008 p=28085d88  HCI MIPC command from CP
t=16555860 publish   h=47501008 p=28085d88  physical ACK
t=16555861 queue     h=17610009 p=28012740  HCI MIPC object
t=16555877 queue     h=17620011 p=28085d88  second HCI object/response
t=16555877 RX/retire h=116a1082 p=30b05001  flash MIPC transport ACK
t=16555878 publish   h=17610009 p=28012740  queued HCI object
t=16555879 snapshot                            flash WRITE E_ERROR
```

The raw CP-shell capture also contains repeated:

```text
bk_ipc_crc_check crc check failed
```

That diagnostic is emitted by the vendor MIPC object CRC check in
`bk_api_ipc.c`; it is not a PPoG sequence CRC or a NOR-flash data CRC. Together
with the retained interleaving above, this localizes the failure to MIPC
descriptor/object ownership or reuse while HCI and flash traffic overlap. It
does not yet prove which exact AP-owned or CP-owned object was modified too
early. The next fix must preserve each MIPC descriptor and pointed-to object
until its matching logical ACK/free event, and must validate the CRC at both
ends before reuse. Do not work around this by dropping large Pebble Protocol
packets or erasing PFS.

Artifacts:

- raw CP-shell/mailbox capture:
  `external artifact: t5ai-watchface-failure-mailbox.raw`;
- post-failure decoded PULSE capture (no new streamed messages):
  `external artifact: t5ai-watchface-failure-after.log`.

A second sync attempt reproduced the same CP MIPC CRC failure continuously.
Its raw capture is
`external artifact: t5ai-watchface-failure-2-mailbox.raw`.
ELF symbol resolution identifies AP object addresses `0x28012440`,
`0x280124c0`, and `0x28012740` as entries in the HCI
`s_event_responses[]` ownership ring; `0x28085dxx` are CP-owned inbound MIPC
objects. The live mailbox recorder shows each AP event response receiving a
physical transport ACK and a later CP MIPC ACK while CRC failures continue.
The ring currently retires entries by object pointer alone and has no
transaction generation capable of rejecting a late or duplicate ACK after
that pointer has been reused. This makes premature slot reuse the leading
specific defect, although the next instrumented build should log the expected
and received CRC plus slot generation before treating it as proven. Do not
perform further phone syncs on this image.

This is the stopping-point handoff for the failed phone watchface-sync session
on 2026-09-14. It supersedes earlier statements that normal Pebble Protocol
transfer or writable PFS operation is proven. Pairing and transport setup work,
but sustained sync that writes BlobDB/PFS does not.

No more firmware was built or flashed after the final trace. The trace process
was stopped. Before the next session, physically disconnect USB power for at
least five seconds so both the AP and CP reset; an AP reset or AP-only flash is
not sufficient to recover the observed CP state.

## Final on-device state

The watch entered a dangerous assertion/reboot loop after phone sync reached
writable storage. The final decoded trace is
`build/traces/pipelined-hci-watchface.log`.

The exact flashed AP artifact was:

- protected image: `build/pebbleos.t5ai-crc.bin`, 928,336 bytes, physical
  range `0x132000..0x214a50`, SHA-256
  `9dc6b52f7e7fd146e2c4523f6a9a73dbdb953a9fb941effbf76763c67649dc9`;
- raw image: `build/pebbleos.bin`, 873,715 bytes, SHA-256
  `ddb4c08190115904399aeeb0cbf8a5220103025bd6b633a200d8cc9c4042b3c0`.

That build includes the T5AI mailbox transmit queue, HCI request and event
response ownership rings, queued ACL transmit ownership, the T5AI-local BLE
connection-parameter policy, and the T5AI blank-check fallback described
below. It is a diagnostic checkpoint, not a release candidate.

## Confirmed trace sequence

The phone successfully completed all of the following before the crash:

1. LE connection, pairing, encryption, and bonding.
2. ATT MTU change from 23 to 256 bytes.
3. Reversed PPoG subscription and Pebble Protocol session open.
4. Initial phone capability exchange and sustained BlobDB traffic.
5. Reception of a large endpoint packet:

   ```text
   T5AI PP RX start ep=0xabcd len=914 sn=29 packet=30
   ```

Large objects are therefore part of the working protocol path and must not be
dropped or truncated as a workaround.

The first fatal event followed filesystem cache recovery during BlobDB work:

```text
Cached header check failed for 'shellpref' (page 178), rescanning
ASSERTION at LR 0x2122547
flash_impl_write_page_begin failed: -1
```

The exact current ELF resolves `0x2122547` to `flash_write_bytes()` in
`src/fw/drivers/flash/flash_api.c:148`. Status `-1` is `E_ERROR` returned by
the T5AI flash implementation after its CP transaction failed.

The first AP restart then reported:

```text
QSPI0 CP power/clock request failed, using AP fallback: -12
Couldn't resolve last written pg
Flash erase failure: -10
```

Status `-12` is `E_AGAIN`; status `-10` is `E_INVALID_OPERATION`. The repeated
assertion LR `0x2122389` resolves to `prv_blocking_erase_complete()` in
`src/fw/drivers/flash/flash_api.c:306`. Each AP restart repeated that erase
failure, producing the dangerous reboot loop.

This was not the earlier NimBLE `ble_att_cmd.c:96` assertion. Queueing ACL
ownership avoided returning transient mailbox backpressure directly into the
NimBLE ATT call site, and the session progressed much farther. It was also not
the earlier connection-parameter callback watchdog. The active-link T5AI
policy avoided that command path.

## Storage condition

The trace contains multiple independent PFS damage indicators:

- `CRC corrupt for page 157` and page 164;
- `Unlinking stale corrupt copy of 'notifstr'`;
- cached-header validation failure for `shellpref`;
- repeated `Couldn't resolve last written pg` after the interrupted write.

An empty app database is expected on this development image and is not itself
an error. These CRC/header/allocator failures are errors. Repeated resets while
PFS mutations were in progress have left storage in a state that can worsen
both boot recovery and phone sync.

Do not erase storage merely to mask the transport problem. After the mailbox
implementation is stable, identify the exact PFS region and, with explicit
operator approval, reformat only that partition. Preserve the AP firmware,
system resources, factory bootloader, CP image, and shared PRF/bond data unless
a separately reviewed recovery plan requires otherwise.

## Mailbox and ownership findings

The original native implementation used one global mailbox mutex and reused
static descriptors across long logical transactions. That design did not
match the pinned Tuya/Beken transport:

- `tuya_tkl_ipc.c` has distinct synchronous and asynchronous mutexes;
- `bk_api_ipc.c` owns IPC objects until their exact completion, uses lists and
  a worker to route received/free/send events, and keeps physical mailbox
  critical sections short;
- `tkl_hci_1.c` relies on those ownership and synchronous-send contracts.

The session changes corrected several real defects:

- `src/fw/drivers/t5ai/cp_mailbox.c` now owns queued outbound frame copies
  until physical completion instead of reusing caller memory prematurely.
- `third_party/nimble/transport/t5ai_hci_ipc.c` owns concurrent request and
  callback-response MIPC objects until matching acknowledgements.
- `third_party/nimble/transport/hci_t5ai.c` accepts NimBLE ACL ownership into
  a T5AI-local queue and retries transient mailbox backpressure.

Those changes improved progress but did not fix the underlying concurrency
contract. `src/fw/drivers/flash/t5ai_flash_ipc.c` still acquires the global
`t5ai_cp_mailbox` mutex for an entire multi-message flash transaction. HCI and
other IPC clients use the same global lock. During simultaneous BLE sync and
BlobDB/PFS activity, one logical client can prevent another from draining or
responding to frames required for forward progress.

The missing PPoG sequence pattern and roughly two-second stalls remain visible
even in the final trace. This is consistent with mailbox starvation or lost
routing, but the exact loss point is not yet proven. Instrument ingress,
handoff, and PPoG sequence counters before claiming a specific layer drops
packets.

The CP survives an AP-only reset. The trace strongly indicates that a failed
flash/mailbox transaction remained wedged on the CP: immediately after the AP
assertion the CP rejected a QSPI vote with `E_AGAIN`, and all subsequent flash
erase attempts returned `E_INVALID_OPERATION`. This explains why AP-only
reboots and flashes did not reliably recover the watch. A physical power cycle
is the current recovery procedure.

## Required architectural fix

### 2026-09-16 Tuya re-audit and clean-driver replacement

The earlier working theory about returning an ACK through the sender's command
buffer was incorrect. Tuya's low-level adapter first copies the foreign
16-byte frame to local storage; `mailbox_channel` mutates that local copy; and
the adapter copies the result again into a receiver-owned transmit slot. The
sender-owned address is never modified or republished.

This distinction is critical on T5AI. Tuya's two 16-byte slots share a
32-byte cache line, while Pebble's cache maintenance operates on complete
lines. Cleaning an in-place ACK at a CP-owned address could therefore write
back over CP's adjacent slot. That is a credible common cause for the lost
ACKs, flash stalls, and reboot-loop behavior seen in this incident.

The replacement implementation is split into board/SoC-local layers:

1. `mailbox_channel.c` owns cache-isolated AP command and ACK slots, permits
   one ordinary physical frame in flight, schedules pending logical channels
   in vendor priority order, and matches completion by channel plus sequence.
   Its IRQ only wakes a highest-priority mailbox task. Foreign addresses are
   invalidated and copied once; no foreign address is ever cleaned or sent.
2. `mb_ipc.c` implements per-socket directional tags, separate transmit and
   receive state, route-status validation, logical responses, and semaphore
   waits. A physical transport ACK is route acceptance only and never
   completes an IPC send.
3. `t5ai_flash_ipc.c` is a thin flash client following the stock request,
   reverse-result, and `READ_DONE` transactions. Reads may be retried by the
   outer T5AI flash layer; ambiguous writes and erases are never retried.
4. CP-to-AP channel `0x4b` is always registered and changes flash-operation
   notification `param1` from request value 1 to ACK value 2, as required by
   the stock CP flash driver.
5. HCI MIPC has its own worker. The mailbox route only snapshots and returns
   the physical ACK; object decoding, logical MIPC ACKs, callbacks, and exact
   object retirement occur outside the mailbox task.

No CP timeout patch is part of this design. Slow flash does not consume the
CP server's 50 ms reverse-SEND window because that wait starts after the flash
operation. A pristine CP should be tested first; a CP change is justified only
if tracing later proves a bounded defect inside the stock CP after the AP
ownership and dispatch rules are correct.

Implement a T5AI-local mailbox dispatcher modeled on the vendor ownership
contract, without changing stock Pebble shared code:

1. Hold physical mailbox ownership only while publishing or retiring a frame.
2. Copy and route inbound frames into per-logical-channel queues from one
   central dispatcher/ISR path.
3. Give every asynchronous outbound frame or MIPC object an owned lifetime
   through its exact physical and logical acknowledgement.
4. Use separate per-client transaction serialization for HCI, flash, PM, and
   SARADC. Never hold a global mailbox mutex while waiting for a logical
   response.
5. Match replies with sequence, tag, channel, and transaction generation so a
   late reply from a timed-out operation cannot satisfy a new operation.
6. Add bounded cancellation/resynchronization for a CP reset or timeout.
7. Expose queue depths, high-water marks, transaction latency, and the exact
   source of `E_AGAIN` in temporary diagnostics.

For flash specifically, move the static flash descriptors behind a flash-only
transaction mutex, route both acknowledgements and logical responses through
the dispatcher, and release physical transport ownership between frames. Do
not weaken the assertions in shared `flash_api.c` as the primary fix.

The T5AI driver also needs a controlled fault state. After repeated
`E_AGAIN`, `E_ERROR`, or `E_INVALID_OPERATION`, stop issuing mutations, mark
external flash unavailable, and keep the system in a board-local degraded or
read-only mode rather than rebooting indefinitely. If documented hardware or
PM IPC provides a safe CP reset, use it through a bounded recovery sequence;
otherwise require a physical power cycle.

The current `flash_impl_blank_check_*()` fallback converts a failed CP blank
check to `S_FALSE` so shared code attempts an erase. That avoided the earlier
blank-check assertion but only moved the failure to write/erase assertions.
Remove or redesign this workaround once the flash fault-state contract exists.

## Bluetooth follow-up

Keep large packets and normal Pebble protocol endpoints enabled. Do not solve
this by dropping OS objects or shrinking AppFetch traffic.

The current T5AI-local connection policy requests a 15 ms, zero-latency link
for `ResponseTimeMin` and uses a 20-second supervision timeout. Whether the CP
accepted that request in the last sync is unknown because the trace began too
late. The next unflashed build logs request acceptance and the controller's
completion result and clears a rejected request's pending state. Low-power
transitions remain disabled. This is an explicit temporary power compromise;
restore stock connection parameters only after sustained sync is reliable,
then measure connected-idle current and battery life.

The ACL queue must not retry forever after a permanent CP failure. Add a
bounded transport-fault transition that cleanly disconnects/resets the T5AI
host without returning a transient error into NimBLE's assertion-prone ATT
send path. Stress the eight-slot HCI request ring and verify every slot retires
on the exact MIPC acknowledgement, including response-before-ACK,
ACK-before-response, timeout, and stale-ACK orderings.

## Scheduler and shared-code safeguards

Do not raise the app task to priority 3 and do not reintroduce a scheduler
guard/polling task. The current working tree has no diff in:

- `src/fw/kernel/event_loop.c`;
- `src/fw/process_management/app_manager.c`;
- `src/fw/board/boards/board_unhygienix.c`.

The shared event loop is stock and the app remains at its stock priority 1.
Any T5AI fix should stay in the board, SoC, mailbox, flash, or transport layer.
The earlier task-priority diagnostic observation was reverted and must not be used as a
phone-sync fix.

## Trace-tool issue

Use the permanent tracer documented in the board guide:

Obsolete command sequence omitted; use the current tooling guide.

Its idle reconnect currently has a host-only race: closing and reopening a
PULSE debug-serial interface can leave a receive thread reading a closed file
descriptor, producing `TypeError: 'NoneType' object cannot be interpreted as
an integer` in `tools/libs/pulse2/pebble/pulse2/link.py`. Repeated reconnects
can also produce overlapping access or permission errors. This noise is not a
firmware crash signature. Fix shutdown so the receive thread is signalled and
joined before closing the descriptor, and do not open a replacement session
until the old one is fully stopped. Until then, `--reconnect-idle 0` is safer
for a stable UART.

## Validation required before another phone attempt

- Host-model interleaved HCI/flash transactions, including both ACK/response
  orders, saturation, timeout, stale reply, and CP reset mid-transaction.
- Queue/ring exhaustion and exact descriptor lifetime tests.
- A hardware soak with artificial flash delays while HCI traffic continues.
- A clean PFS provision only after explicit approval and exact range review.
- Repeated initial sync and large watchface AppFetch/PutBytes transfers with
  sequence/drop counters and no filesystem errors.
- Cold power-cycle and reconnect tests, followed by low-power restoration and
  current measurement.
- Verification that shared event-loop/app-manager code and task priorities are
  unchanged.

The 89-test host suite passed before the final flash, but those tests did not
model concurrent CP mailbox clients or prove recovery after a CP-side wedged
transaction. Passing them is necessary, not sufficient.

## 2026-09-16 implementation checkpoint (not yet flashed)

The T5AI-local transport redesign above is now implemented in the working
tree. This image has **not** been written to hardware, so none of the following
is a hardware result:

- One mailbox task owns the hardware and is the only runtime publisher. Each
  logical AP-to-CP channel has one AP-owned, cache-aligned pending slot; only
  one ordinary physical frame is in flight, and pending channels are selected
  in ascending vendor priority order.
- CP-to-AP frames are invalidated and copied before dispatch. Their physical
  acknowledgements are built in separate AP-owned 32-byte slots. No CP-owned
  address is modified, cleaned, or republished.
- Physical completion is matched by logical channel and sequence. A physical
  ACK proves routing only; socket-level sends remain pending until the matching
  command, directional ports, and tag receive a logical response.
- Flash/SARADC, synchronous HCI, asynchronous HCI callbacks, and control/PM
  state now have separate client serialization. Flash no longer owns a global
  mailbox lock across its multi-message transaction.
- Every HCI request slot owns its message, descriptor, and frame. Logical MIPC
  acknowledgements must match the descriptor pointer and its current CRC;
  timed-out slots remain quarantined until a new controller generation starts.
- HCI fault state is atomic across the host and worker tasks. Command and ACL
  failures converge on one guarded NimBLE reset rather than retrying forever or
  scheduling duplicate resets.
- A malformed-size HCI mailbox frame is now negatively acknowledged and
  rejected. It can no longer be returned as success with an uninitialized
  frame.
- Flash transactions follow request, reverse result, and `READ_DONE` exactly,
  match logical response tags, snapshot mailbox diagnostics on failure, and
  fail closed for the remainder of that boot. Ambiguous writes and erases are
  not retried.
- The T5AI connection policy requests 15 ms with zero slave latency and logs
  both submission and controller completion so the next trace can prove what
  was accepted.

Validation completed at this checkpoint:

- 62 focused Getafix, mailbox/MIPC ownership, flash, and board tests pass;
- `git diff --check` passes;
- a full `waf build` passes with 877,363 bytes of 960 KiB FLASH (89.25%) and
  89,248 bytes of 124 KiB KERNEL_RAM (70.29%);
- `build/pebbleos.bin`: 877,363 bytes, SHA-256
  `c026df474d676caa79293d5828b964e42bbe02b5af6b5b01f8208cd6a951e8d3`;
- `build/pebbleos.t5ai-crc.bin`: 932,212 bytes, SHA-256
  `3a145509574153646e9b4d27f29b89d2537ebba763cc78fa4ff948d4b477fcf8`.

The combined `pebbleos.t5ai-flash.bin` predates this build and must be
regenerated before any hardware validation; its old hash is intentionally not
recorded as a candidate image.

The first hardware run should be deliberately narrow:

1. Cold power-cycle the watch so CP0 cannot retain the previous wedged flash
   transaction.
2. Regenerate the combined image, record its hash, flash that exact image once,
   and immediately start `tools/unhygienix_trace.py --reconnect-idle 0`.
3. Establish the phone link without initiating a sync; verify the requested
   connection interval completes and the mailbox diagnostics stay balanced.
4. Start one watchface sync and capture the first transport fault, PPoG
   sequence discontinuity, or flash failure. Do not repeat sync attempts after
   a flash fault in the same boot.
5. If transport remains healthy but PFS recovery fails, stop and review an
   exact-range PFS-only reformat separately. Do not erase boot, CP, AP,
   resources, or shared PRF/bond data.

One recovery limitation remains: the T5AI flash client now stops issuing CP
mutations after a transaction fault, but stock shared `flash_api.c` still
asserts when a required write or erase fails. The transport changes should
prevent the concurrency failure that triggered that path; they do not yet
provide a complete read-only UI mode after genuine external-flash loss. Do not
hide failed writes as successful. If hardware can still reproduce a flash
fault with balanced mailbox/HCI traces, the next scoped change is a T5AI-local
storage-unavailable state that keeps boot/UI alive without retrying mutations.

## 2026-09-16 PutBytes and physical-ACK ownership follow-up

The first hardware run of the diagnostic fixed-PSRAM reversed-PPoG ingress
path got through phone setup, AppFetch, PutBytes initialization, and the first
2,009-byte watchface block. It then asserted in `flash_write_bytes()` after
`flash_impl_write_page_begin()` returned `E_INVALID_OPERATION`. This showed
that the diagnostic queue could carry the burst, but did not prove that the
stock heap path was intrinsically undersized or justify retaining a parallel
T5AI transport. It localizes that run's terminal failure to the AP/CP flash
path while HCI traffic is active. The capture is
`build/traces/t5ai-watchface-putbytes-psram-20260916.log`.

Review against Tuya's `mailbox_channel.c` found one remaining transport
ownership error: Pebble used a global two-entry ring for physical ACK messages,
but the hardware FIFO carries pointers rather than copying message contents.
Unrelated HCI and flash receive channels could therefore overwrite a slot
before CP dereferenced it. Tuya keeps each logical channel BUSY until that
channel's ACK is consumed. Pebble now gives every receive channel one aligned,
stable ACK slot, matching that lifetime rule without adding a timeout or
serializing HCI behind flash.

The follow-up image is built and validated but has not yet been flashed:

- 90 focused Getafix/T5AI tests pass and `git diff --check` passes;
- FLASH is 877,555 bytes and KERNEL_RAM is 86,464 bytes;
- `build/pebbleos.bin` SHA-256 is
  `bac1084852552a332401b121313a2cb5584670ceb5438a29b644c9d38443f67c`;
- `build/pebbleos.t5ai-crc.bin` SHA-256 is
  `d4753b31ce7bcc1bd74080246fab667742ac211404594b83e5ad8a23ae4785c2`;
- the exact AP-only physical range is `0x132000..0x215a40` (end exclusive).

Cold-power both processors before flashing and run one traced sync. Do not
reuse the current CP session after the flash fault.

## 2026-09-16 reversed-PPoG heap measurement correction

The fixed-PSRAM reversed-ingress queue and its adapters have been removed.
The stock NimBLE allocation and `bt_driver_cb_ppog_reversed_data_written()`
handoff are restored. The T5AI kernel heap spans about 40,192 bytes, while the
expected 17-packet receive burst needs only about 4.5--5 KiB including payload,
callback contexts, and allocator overhead. A second ingress architecture is
therefore not warranted without measurements showing that the stock path
cannot support the bounded burst.

The next image adds T5AI-only diagnostics around the unchanged stock path. It
records total heap size, current used/free bytes, largest free block, minimum
headroom, outstanding reversed-PPoG callbacks, peak outstanding callbacks,
and enqueue-to-KernelMain drain latency. Samples are taken at reversed-PPoG
subscription, immediately before both payload and callback-context
allocations, and on either allocation failure. This distinguishes aggregate
exhaustion from fragmentation and from a delayed KernelMain consumer.

Validation for this measurement image:

- all 90 documented Getafix/T5AI pre-flash tests pass and `git diff --check`
  passes;
- FLASH is 877,971 bytes, KERNEL_RAM is 86,464 bytes, and
  T5AI_DISPLAY_RAM is 87,168 bytes;
- `build/pebbleos.bin` is 877,971 bytes with SHA-256
  `2f0db26e955ac14172738af99fa274de4a86030a77e5e949431077de24b514bf`;
- `build/pebbleos.t5ai-crc.bin` is 932,858 bytes with SHA-256
  `baa6159c6213ae8c3efd86391254cde36a694b10d004c3f1180e756db920a2d8`;
- the combined `build/pebbleos.t5ai-flash.bin` still predates this build and
  must be regenerated before flashing.

Do not make another transport change before capturing one cold-boot sync with
these counters. If allocation pressure is confirmed, the preferred correction
order is: combine payload and deferred context into one owned allocation; make
a small T5AI-local kernel-heap sizing adjustment only if measured headroom
requires it; then, only if necessary, reduce the T5AI reversed-PPoG receive
window so the phone throttles its Write Without Response burst.

## 2026-09-16 measured heap correction

The measurement run confirmed allocation pressure rather than a slow
KernelMain consumer. At reversed-PPoG subscription, the 40,512-byte kernel
heap had only 4,072 bytes free. During ordinary GATT discovery it fell to
2,472 bytes free with a largest contiguous block of 488 bytes. The negotiated
17-packet receive window needs about 4.5--5 KiB. In contrast, the deferred
callback depth peaked at one packet; normal drain latency was 1--3 ms, with
startup outliers of 73 ms and 144 ms. The capture is
`build/traces/t5ai-ppog-heap-measurement-20260916.log`.

The phone ended that session before the trace showed AppFetch, but the next
cold boot found many incomplete PFS files and then stalled before Bluetooth
startup. Its current IPC snapshot was `senderr=10`, `rxto=0`, with no retained
flash-failure record. Do not treat the resulting phone connection failure as a
new BLE or PPoG failure: the watch never reached advertising on that boot.

The resulting correction keeps the stock heap-backed callback architecture:

- payload and `ReversedDataCtx` are one allocation with one ownership lifetime;
- the T5AI Gabbro AppState reservation is reduced by 3 KiB, from 77,824 to
  74,752 bytes, retaining 816 bytes above the measured 73,936-byte AppState;
- kernel RAM grows from 124 KiB to 127 KiB and the kernel heap grows from
  40,512 to 43,584 bytes;
- the RX window remains 17; no PSRAM ingress queue or parallel transport is
  present.

Validation for the corrected candidate:

- all 90 documented Getafix/T5AI pre-flash tests pass;
- the full firmware build and T5AI image-map guard pass;
- `git diff --check` passes;
- `build/pebbleos.bin` is 877,907 bytes with SHA-256
  `cb2d83db909d932c3b7d0becbed7361ffffdedfa3d8923d9c67cf4ebaec39d1d`;
- `build/pebbleos.t5ai-crc.bin` is 932,790 bytes with SHA-256
  `a36cd0ffb4975ab746ef4497ddbff1941c967c7384666088f64ccc11d88a3e69`;
- `build/pebbleos.t5ai-flash.bin` is 3,268,608 bytes with SHA-256
  `bdd6160792269635b7acd9f324687e2331391e2dae3410ba786eaeaafef15921`.

The next flash must reformat the already-incomplete PFS, then capture one
cold-boot connection and watchface sync. Do not reduce the RX window unless
this measured-capacity build still records packet allocation failure.

## 2026-09-16 AppState-boundary diagnostic observation rejected

The 3 KiB AppState-to-kernel rebalance above is not an acceptable fix. On the
first hardware boot with the shifted worker and app boundaries, the watch
reached `Ready for communication` and then suffered a complete AP stall before
the phone connected or any reversed-PPoG traffic occurred. Runtime UART output
stopped and neither the `PBLIPC` nor `PBLTIM` liveness command received an AP
response. The capture is
`build/traces/t5ai-ppog-single-allocation-20260916.log`.

The memory map has therefore been restored to its previously exercised
boundaries: 124 KiB kernel RAM, worker RAM at `0x2802f000`, and app RAM at
`0x28032000` with a 77,824-byte Gabbro runtime reservation. Do not move those
boundaries again without separately proving the worker/app runtime contracts.

The heap measurements do support retaining the single-allocation ownership
change by itself. The lowest observed aggregate free space was 1,388 bytes,
the largest observed free block was 488 bytes, and reversed-PPoG callback depth
peaked at one. A packet needs less than 300 bytes. The old path first allocated
the packet payload and then allocated a second payload-sized deferred context,
so a 488-byte largest block could satisfy the first allocation and leave no
block large enough for the second. The new path needs one block and transfers
that allocation directly to KernelMain. No timer-pool reduction, NimBLE-pool
reduction, RX-window change, PSRAM ingress queue, or memory-boundary change is
included in the next candidate. Heap and drain diagnostics remain enabled to
validate this conclusion during one traced sync.

Validation for this restored-layout candidate:

- all 90 focused Getafix/T5AI tests pass and `git diff --check` passes;
- FLASH is 877,907 bytes, KERNEL_RAM is 86,464 bytes of the original 124 KiB
  region, and T5AI_DISPLAY_RAM is 87,168 bytes;
- `build/pebbleos.bin` SHA-256 is
  `75ace5d2077f43184b49595fd99f53c234a19dc9158667ecef4d7e1d677f59ed`;
- `build/pebbleos.t5ai-crc.bin` SHA-256 is
  `dc41c83827e1f2753d2d00715b9b29008739d11697676d5cd89db5127480afac`;
- regenerated `build/pebbleos.t5ai-flash.bin` SHA-256 is
  `c70e465284d3ab4f15b98d778367d313851f66fc64edbccbaffe496c65d743f8`.

## 2026-09-17 end-of-day handoff

### What is now known

- Slow external-flash writes are not the cause of the Bluetooth receive
  failure. Reversed-PPoG normally drained into KernelMain in 1--3 ms and the
  measured callback depth peaked at one. The 73 ms and 144 ms startup outliers
  did not create a sustained backlog.
- The stock two-allocation receive path was vulnerable to kernel-heap
  fragmentation. The heap still had aggregate free space, but its largest
  observed block fell to 488 bytes. Allocating the payload and then another
  payload-sized deferred context could therefore make the second allocation
  fail. One allocation containing both context and payload directly addresses
  this measured mechanism.
- A fixed PSRAM ingress queue and T5AI-specific reversed-PPoG adapters are not
  justified. They have been removed. The normal NimBLE-to-kernel callback path
  is retained, with only the ownership allocation combined.
- Flash and HCI share a mailbox whose hardware FIFO carries pointers. The old
  global two-slot physical-ACK ring could be overwritten by traffic from
  another logical channel before CP consumed it. T5AI now owns one stable ACK
  slot per receive channel, matching the Tuya channel lifetime rule.
- Moving the worker/app boundary to gain another 3 KiB of kernel heap is
  rejected. That image caused a full AP stall before any BLE connection. This
  was a memory-layout regression, not a sync result.
- Reducing the TaskTimer or NimBLE pools is also deferred. The TaskTimer pool's
  historical all-system peak is 111 entries and exhaustion currently asserts;
  NimBLE's T5AI MSYS and inbound ACL pools were previously increased in
  response to measured pairing/discovery and AppFetch pressure. No pool
  reduction is needed to test the measured one-allocation fix.

### Current source and hardware state

The production source is `the repository root`. Shared Pebble code
has not acquired a parallel T5AI transport; the material changes in the current
candidate are the T5AI per-channel ACK ownership, one owned reversed-PPoG
allocation, and T5AI-only heap/drain diagnostics. The original 124 KiB kernel,
12 KiB worker, and 208 KiB app regions are restored.

The watch is still running the rejected boundary-shift image and is AP-frozen.
Its runtime UART stopped and `PBLIPC`/`PBLTIM` received no response. No further
diagnostic state is recoverable from that boot without a hardware debugger.
At the final host check no `/dev/ttyACM*` device was present. Do not attempt a
phone sync on this image; physically replug/cold-power the board first.

The restored-layout candidate has been rebuilt, all 90 focused tests pass, the
image-map check passes, and `git diff --check` passes. The exact combined image
to test is `build/pebbleos.t5ai-flash.bin`, 3,268,608 bytes, SHA-256
`c70e465284d3ab4f15b98d778367d313851f66fc64edbccbaffe496c65d743f8`.
It contains firmware, system resources, and a preformatted empty PFS. Flashing
it is therefore intentionally destructive to current PFS contents but does
not target the bootloader, CP image, or shared PRF/bond partition. Obtain fresh
explicit approval before writing it.

### First run tomorrow

1. Replug/cold-power the watch and verify the programming and runtime UARTs
   enumerate. Do not reuse the frozen AP/CP session.
2. After explicit approval, verify the combined-image hash above and flash that
   exact image once.
3. Start a persistent runtime trace immediately and let the watch reach a
   stable idle/advertising state before involving the phone.
4. Connect the already-paired phone without starting AppFetch. Confirm the AP
   remains live, the connection interval completes, and mailbox counters stay
   balanced.
5. Start exactly one watchface sync. Preserve the first allocation failure,
   PPoG sequence discontinuity, flash error, assertion, or reset. Do not repeat
   a sync after a flash fault in the same boot.
6. A successful transfer must be followed by launching the installed
   watchface and then disconnecting/reconnecting once; transport completion
   alone is not sufficient validation.

The remaining uncertainty is hardware validation of this exact candidate. If
its diagnostics show an allocation failure, measure the requested size, free
bytes, largest block, pending depth, and drain time before changing capacity.
If allocation succeeds but flash fails, stop changing Bluetooth and implement
the scoped T5AI storage-unavailable/read-only state before another sync. Only
consider a smaller receive window or a measured T5AI TaskTimer default after
the corresponding trace proves it is necessary.

## 2026-09-18 four-packet receive-window candidate

The restored-layout, single-allocation candidate was flashed after a cold
power cycle and tested with one phone connection and one watchface AppFetch.
Cold boot formatted the empty PFS, validated system resources, restored the
saved Bluetooth bond, advertised, and reached `Ready for communication`.
The phone then encrypted successfully, negotiated MTU 256 and a 15 ms
connection interval, completed GATT discovery, and opened reversed PPoG with
TX window 4 and RX window 17.

The AppFetch trace proved that one allocation per packet was necessary but not
sufficient. Deferred PPoG depth rose to seven, drain latency reached 128 ms,
free heap fell to 696 bytes, and the largest free block fell first to 208 bytes
and then 76--100 bytes. Multiple 266--269-byte packet allocations failed,
followed by sequence discontinuities and the expected PutBytes timeout. There
was no flash error, assertion, or reboot. The complete capture is
`build/traces/t5ai-bluetooth-resume-connect-20260918.log`.

This is the measured threshold required by the previous handoff before
changing protocol capacity. Unhygienix now selects the established
four-packet PPoG receive window instead of the stock 4.5 KiB-derived window
through `CONFIG_PPOGATT_RX_WINDOW_SIZE`. The shared protocol code has no T5AI
branch, and every other board retains the stock default of 17. The smaller
window makes the phone apply backpressure before the deferred KernelMain queue
can reproduce the observed seven-packet heap collapse. The single-allocation
diagnostic observation has been removed: shared `ppogatt.c` and its driver callback API are
restored to stock ownership. Only the T5AI NimBLE service samples payload
allocation headroom, while the original memory boundaries remain unchanged.

Validation for the new candidate:

- all 90 focused Getafix/T5AI tests pass;
- the full firmware build and image-map guard pass;
- `git diff --check` passes;
- `build/pebbleos.bin` is 877,715 bytes with SHA-256
  `986010b2a42a6995d5cfecb3649059cbe69c67138d06909910aa51b8a7e7cabe`;
- `build/pebbleos.t5ai-crc.bin` is 932,586 bytes with SHA-256
  `bc2fae91744d1a90f307b5c334a7d138a7ba83ad45db36ccc2232630ec975033`;
- `build/pebbleos.t5ai-flash.bin` is 3,268,608 bytes with SHA-256
  `9e0c9eeefcb6b114dad17bea12c7a310fe26c680a3ff8c1f3584afb2dcd83cba`.

This candidate has not yet been flashed. Its first hardware run should again
use one cold boot, one phone connection, and one watchface sync. The session
must negotiate RX window 4. Stop on the first packet-allocation failure,
sequence discontinuity, flash error, assertion, or reboot. If the transfer
completes, launch the installed watchface and disconnect/reconnect once.

## 2026-09-18 RX-window result and T5AI MIPC ACK candidate

The four-packet candidate was flashed and tested with one watchface sync. The
phone negotiated RX window 4. During the transfer, 250--253-byte PPoG payload
allocations succeeded and the largest free kernel-heap block remained between
972 and 1,232 bytes. There was no PPoG allocation failure or sequence
discontinuity. This validates the smaller Unhygienix receive window against the
measured Bluetooth-ingress failure without changing shared `ppogatt.c`.

The transfer then exposed the independent T5AI mailbox fault at
`18:56:21.552`: `flash_impl_write_page_begin` returned
`E_INVALID_OPERATION` (`-10`) and shared `flash_api.c` asserted. The trace is
`build/traces/t5ai-rxw4-connect-20260918.log`. The following reboot could not
resolve the interrupted PFS write and the first NimBLE restart also asserted;
the second reboot eventually reconnected. No second sync was attempted.

The USB bridge was no longer enumerated when diagnosis resumed, so the
retained `PBLIPCF`/`PBLIPCFE` snapshot could not be recovered. The saved
September 15 evidence remains applicable: overlapping HCI and flash traffic
produced CP-side MIPC CRC failures, and HCI response slots were retired by
descriptor address alone. `T5AIMIPCData` CRC deliberately excludes the
CP-owned list links and result field, so those legitimate mutations do not
justify ignoring the CRC.

The next test candidate therefore changes only the T5AI HCI transport. A
logical ACK must now match both the descriptor address and the current CRC of
the immutable descriptor fields before the slot can be retired. This prevents
a late or duplicate ACK for a prior occupant of the same ring slot from
freeing a new transaction. A mismatch leaves the slot quarantined; bounded
slot exhaustion then enters the existing HCI fault/reset path instead of
silently corrupting cross-client mailbox ownership.

Validation for this unflashed candidate:

- all 89 currently discovered focused Getafix/Tuya tests pass;
- the full firmware build and image-map guard pass;
- `git diff --check` passes;
- `build/pebbleos.bin` is 877,715 bytes with SHA-256
  `399494f66fef6a8cc45351dcd34fd40d16f665febbab55c12b7b3efb72687429`;
- `build/pebbleos.t5ai-crc.bin` is 932,586 bytes with SHA-256
  `bdea5444ec5fded3c24cd6d48d3e381338d4906b97caf6ac5d59c88f066f955a`;
- `build/pebbleos.t5ai-flash.bin` is 3,268,608 bytes with SHA-256
  `a004a018a1117bd60ff9c8f81eb98db5d1801d05d6e04bbfc1ac8cf00dbd7fc5`.

Do not flash or retry AppFetch until the dual-UART bridge is present. This is
an instrumented test candidate, not evidence that the MIPC ownership defect is
fixed. On the next run, capture the raw CP shell as well as PULSE output and
retrieve `PBLIPC`, `PBLIPCF`, and `PBLIPCFE` immediately after the first fault.

### 2026-09-18 receive-window decision superseded

The receive-window Kconfig and Unhygienix override described above were removed from
the active candidate. The four-packet run remains useful capacity evidence,
but the preferred fix is now to audit and remove T5AI kernel-memory waste while
retaining the stock window of 17. Static allocation, task-stack high-water
marks, queue/pool sizing, transient buffers, diagnostics, and fragmentation
during AppFetch must be measured before changing protocol capacity. The
four-packet Kconfig design is recorded only as a later fallback in
`boards/unhygienix/tuya/REMAINING_TASKS.md` if the audit cannot recover sufficient
contiguous headroom. Shared `ppogatt.c` remains stock.

### 2026-09-18 T5AI kernel-memory audit candidate

The linked 124 KiB kernel region previously contained 86,464 bytes of static
data and left a 40,512-byte kernel heap. The dominant board-relevant static
consumers were NimBLE MSYS backing storage (10,220 bytes), transport ACL
backing storage (9,600 bytes), the TaskTimer pool (7,680 bytes), privileged
app/worker syscall stacks (4,096 bytes), and T5AI HCI request/response rings
(2,560 bytes). Existing evidence does not justify shrinking those capacities:
the TaskTimer historical peak is 111 entries, and the NimBLE MSYS and inbound
ACL counts were raised after measured pairing/discovery and AppFetch pressure.

The MSYS and ACL backing arrays themselves are AP-only storage. T5AI's HCI
transport copies inbound and outbound data at the CP ownership boundary; CPU0
never dereferences these NimBLE pool blocks. The T5AI linker now places only
`.bss.os_msys_1_data` and `.bss.pool_acl_buf` in the established cacheable
`.t5ai_board_cache` PSRAM section while leaving pool metadata and every
CP-shared HCI message/descriptor in internal RAM. Both pools retain their
existing counts and block sizes. `os_mempool_init()` initializes each NOLOAD
backing block before Bluetooth use.

The resulting map uses 66,656 bytes of KERNEL_RAM and 107,008 bytes of the
128 KiB T5AI PSRAM reservation. The kernel heap therefore grows from 40,512
to 60,320 bytes, recovering 19,808 bytes without moving the worker/app
boundaries or reducing any pool. A read-only `PBLTIM` sample from the preceding
image reported 577 words free on KernelMain, 803 on KernelBackground, 156 on
NewTimers, and 672 on the app task; these are useful observations but not yet
enough worst-case evidence to resize stacks.

The active source again uses the stock PPoG receive window of 17 and has no
receive-window Kconfig. Host validation passes all 91 focused Getafix/T5AI
tests, the full build and image-map guard, and `git diff --check`. The unflashed
protected AP image is 932,586 bytes with SHA-256
`94579a0db696ecde703704ebccf6fe26e335d49a0b457f6e2fb63ec30aae59a7`.
The old `build/pebbleos.t5ai-flash.bin` is stale and must not be flashed; the
flash runner must regenerate it from this protected AP image and current
system resources immediately before an approved test.

### 2026-09-18 strict MIPC ACK diagnostic observation rejected

The first flashed memory-layout image also contained the preceding strict HCI
ACK-CRC diagnostic observation. It entered a reboot loop before phone activity: HCI command
transmits failed repeatedly, NimBLE host startup timed out, and the CP shell
reported continuous `bk_ipc_crc_check crc check failed` diagnostics. The trace
is `build/traces/t5ai-stock-rx17-psram-pools-coldboot-20260918.log`.

This disproves the assumption that the CRC-covered portion of the vendor MIPC
descriptor is immutable until ACK. The CP changes descriptor ownership state
before returning the logical ACK, so its CRC is diagnostic and cannot serve as
a transaction-generation token. Strict CRC retirement has been removed;
pointer/type matching is restored. The PSRAM pool relocation is independent:
those AP-only NimBLE backing blocks are never presented to the CP. Rebuild and
test the relocation alone before drawing any conclusion about it.

### 2026-09-18 stock-window PSRAM-pool hardware result

The corrected memory-audit candidate cold-booted, restored the bond, connected,
negotiated MTU 256 and a 15 ms interval, completed discovery, and opened
reversed PPoG with the stock version-1 receive window of 17. Normal BlobDB
traffic and the beginning of one AppFetch carried repeated 253-byte packets
without an allocation failure or sequence discontinuity. The linked kernel
heap was 60,320 bytes; the observed minimum free heap was 18,524 bytes and the
largest free block immediately before the terminal fault remained 19,252
bytes. This validates the board-local pool relocation against the measured
Bluetooth ingress failure and leaves the four-packet Kconfig design as a
fallback only.

The run then reproduced the independent flash-path failure at
`19:31:30.191`: `flash_impl_write_page_begin()` returned
`E_INVALID_OPERATION` (`-10`) and shared `flash_api.c` asserted. The exact ELF
resolves LR `0x21227ef` to `flash_write_bytes()` at `flash_api.c:148`. There
was no preceding heap, PPoG sequence, mailbox-timeout, or ACK-mismatch fault in
the decoded trace. The complete capture is
`build/traces/t5ai-stock-rx17-psram-pools-connect-20260918.log`.

The T5AI flash driver now logs whether a failed write came from its local
address/protection policy or from the CP flash IPC session, including the
address, page length, command, and status. This is diagnostic only; shared
flash behavior and the PPoG window are unchanged. Do not retry AppFetch on the
same flashed image because the pre-assert origin was not observable there.

### 2026-09-18 retries stop before AppFetch

The flash-origin diagnostic image did not reproduce the earlier flash write.
After bond restoration and discovery, the first retry carried no AppFetch or
PutBytes traffic and ended about 92 seconds later with HCI reason `0x08`
(connection supervision timeout). A cold-replugged retry restored 97 BlobDB
packets; a later phone attempt delivered a 914-byte App Order packet on
endpoint `0xABCD`, then ended about 52 seconds later with the same supervision
timeout. Neither run entered AppFetch, called the flash writer, asserted, or
reset. The latter trace is
`build/traces/t5ai-stock-rx17-flash-origin-retry-20260918.log`.

The raw `PBLIPC` snapshot after the second timeout was
`tx=2087/1850/1849 ... senderr=237 rxto=0 ackmis=0`. The 237 send errors are
not 237 dropped frames: the HCI sender retries `E_BUSY` once per millisecond
for up to two seconds, and `tx_queued - tx_published` is also 237. Existing
mailbox counters therefore conflate harmless single-channel contention with a
terminal send failure.

The next diagnostic candidate adds T5AI-local HCI counters and logs only; it
does not alter mailbox, HCI, PPoG, or flash behavior. It separates busy retries
from terminal send and exchange timeouts, records request/response slot and
pending-frame pressure, counts controller events and ACL packets, and emits a
snapshot when HCI Disconnection Complete arrives. The focused 66-test
Tuya/Getafix suite and full firmware build pass. KERNEL_RAM use is 66,688
bytes, 32 bytes above the preceding diagnostic image. `build/pebbleos.bin` is
878,259 bytes with SHA-256
`be3e4e74eb68df25b3fcf70193d9b08dfde288cdf2b28cca5dce546ac55613e9`.

### 2026-09-18 HCI isolation and transfer-triggered reboot

The HCI diagnostic image connected at a 15 ms interval and remained connected
for about 62 seconds before controller reason `0x08`. Its disconnect snapshot
reported 677 received frames, 338 mailbox acknowledgements, 119 commands, 127
events, 93 ACL packets, 220 responses, 109 transient busy retries, no terminal
send or exchange timeouts, and no request-slot exhaustion. This isolates that
supervision timeout from AP/CP mailbox congestion, heap pressure, and PPoG
sequence handling. The trace is
`build/traces/t5ai-hci-diagnostics-sync-20260918.log`.

A subsequent single-watchface attempt reproduced an AP reboot, but the live
decoder emitted no pre-reset fault line. Two diagnostic persistence gaps were
identified: the mailbox failure snapshot is ordinary BSS despite being treated
as reset-retained, and boot logging clears the retained reboot reason before a
later shell query can retrieve it. T5AI persistent coredumps are also disabled.

The next diagnostic image captures the reboot reason and assert extra value at
early boot, and records T5AI flash policy or IPC-write failures in unused
retained words before returning the error. `PBLTIM` reports these as
`rr=<code>/<extra>` and `ff=<context>/<status>/<address>`, then clears the flash
record so it describes only the preceding boot. This is T5AI-local diagnostics
only and does not change transfer, flash, mailbox, HCI, or PPoG behavior. The
focused 67-test Tuya/Getafix suite and firmware build pass. KERNEL_RAM use is
66,720 bytes. `build/pebbleos.bin` is 878,483 bytes with SHA-256
`6ca4f576dcb3eebaf8a901af703f34f701be5fa9ae52fbb1d2caa60a6760feb7`.

### 2026-09-18 retained-diagnostic watchface result

One watchface transfer on the retained-diagnostic image reproduced the primary
failure. AppFetch began with about 20.7 KiB free heap and a 19.2 KiB largest
block, so allocation pressure was not causal. The flash client then reported
repeated `FLASH_READ` command 3 operations as unavailable, followed by an
unavailable `FLASH_WRITE` command 5. `flash_impl_write_page_begin()` returned
`E_INVALID_OPERATION` (`-10`) and shared `flash_api.c` asserted. The trace is
`build/traces/t5ai-retained-diag-watchface-20260918.log`.

The first AP restart left the CP Bluetooth controller running. NimBLE restart
attempts returned reason `0x0013`, then asserted on host-start timeout at LR
`0x021246b7`, causing a second AP restart. `PBLTIM` subsequently reported
`rr=17/021246b7`, confirming that secondary assertion. The retained flash
record had been consumed during the short first boot and was therefore zero by
the second boot, but the live trace preserved the primary flash failure.

Comparison with the pinned Tuya/Beken flash client found the concrete recovery
divergence. The vendor client retries a failed non-mutating read once. Pebble's
outer T5AI layer also nominally retries reads, but the inner client poisoned
the socket after every failed `READ` or `READ_DONE`, so the retry could only
return `E_INVALID_OPERATION`; the later write then failed without being sent.

The next candidate allows one socket reconnect on the existing outer retry
path only when the preceding fault was `READ`, `READ_DONE`, or initial connect.
It does not retry or reconnect after an ambiguous write or erase: mutations
remain failed closed for the rest of that boot. First-fault logging now records
the command, status, and recoverability, and the T5AI write diagnostic avoids
the unsupported `%zu` formatter. The vendor MIPC CRC byte range was verified
against both AP and CP reference source and remains unchanged.

The focused 68-test Tuya/Getafix suite, `git diff --check`, and full firmware
build pass. KERNEL_RAM remains 66,720 bytes. `build/pebbleos.bin` is 878,579
bytes with SHA-256
`4315639338a8397a17fccd1b9a068ce85f0b5118d6a44560a48d302fb1de1d2f`;
the protected AP image has SHA-256
`72dc52f310e003118f05a3123f8e4770f02f3124dd3e3f7cb6cb9232104520f7`.

### 2026-09-18 flash-recovery watchface result

The read-reconnect candidate prevented the previous flash assertion and AP
reboot. The initial phone sync paused, then a phone retry transferred the
watchface twice. Both AppFetch attempts completed with result 0, no flash IPC
fault, and about 19.8 KiB minimum free heap. Immediately after each successful
transfer, however, app 25 failed resource lookup and AppFetch started again;
the watch eventually remained on the AppFetch UI while the Bluetooth task was
still able to report a clean disconnect. The capture is
`build/traces/t5ai-flash-read-recovery-watchface-20260918.log`.

The failure was a T5AI resource-backend configuration gap. T5AI PutBytes
accepts `ObjectAppResources` and stores it as a PFS app-resource file, but the
T5AI branch of `resource_impl.def` registered only the built-in and system-bank
backends. Consequently, every transferred app resource file was invisible to
`resource_storage_check()` after PutBytes closed it. T5AI now registers the
existing `g_app_file_impl` ahead of the built-in and system-bank backends. This
is SoC-scoped and restores the same app-resource lookup path used by stock
normal firmware without enabling the unrelated generic file backend.

### 2026-09-19 launch-fault root cause and successful watchface run

With app-resource lookup restored, the phone completed a watchface transfer,
but launching the installed app reset the AP. The fault was not another flash,
PPoG, heap, or resource-storage failure. T5AI routes normal debug output through
a synchronous CP mailbox transport. An interrupt-time log attempted to enter
that transport, which takes a task mutex and waits for a CP acknowledgement.
The resulting assertion occurred during app launch.

The production fix is deliberately two-layered and T5AI-scoped:

- `dbgserial_putchar_lazy()` and `dbgserial_flush()` discard interrupt-time
  T5AI output without modifying any partially assembled task-context message;
- `t5ai_cp_mailbox_log()` independently rejects interrupt context with
  `E_INVALID_OPERATION` so a future direct caller cannot block in an ISR.

No shared mutex or assertion behavior was changed. A second apparent app fault
was caused by temporary stage instrumentation writing a privileged kernel
global after the app task had become unprivileged; the stage writes were
removed rather than changing MPU boundaries.

The resulting instrumented image had raw SHA-256
`8933eb40e42cc18556144d17020a73be8782eddba423c9227aa27e074a3708dc`.
It cold-booted, connected, opened PPoG with `TXW: 4 RXW: 17`, transferred a
23,817-byte watchface, validated its metadata, launched it, and remained stable
without a reset, freeze, mailbox timeout, or flash failure. Minimum observed
PPoG heap headroom was approximately 7.9 KiB. This is the first successful
end-to-end stock-window watchface transfer and launch on Unhygienix.

### 2026-09-19 diagnostic cleanup and flashed confirmation candidate

After the successful run, the high-volume investigation-only instrumentation
was removed: PPoG heap sampling, decoded HCI/BlobDB packet progress, app
transfer/cache/launch success logs, boot/app/launcher stage probes, and PFS boot
tracing. Failure-only HCI and flash logs, disconnect snapshots, mailbox failure
counters, retained failure reporting, and the on-demand `PBLTIM` diagnostics
remain. The functional ISR logging guards remain.

The cleanup reduced FLASH by 1,664 bytes, KERNEL_RAM by 32 bytes, and
LOG_STRINGS by 3,183 bytes relative to the immediately preceding build. The
focused 70-test suite, full firmware build, image-map checks, and both staged
and unstaged `git diff --check` passed. The final artifacts are:

- raw image: 878,035 bytes, SHA-256
  `19bd10aaac8fd6097b8e51dc99e1195b0c6a7a0957e7aae3a7651e04b99b846d`;
- protected AP image: 932,926 bytes, SHA-256
  `c4cf00676e51f7d12bcd42e9792e828044665f49c3ea52653a3cf7be5159fb63`;
- exact AP-only physical range: `0x132000..0x215c3e` (end exclusive).

The protected image was validated by `tools/flash_unhygienix_firmware.py` and flashed
successfully through `if00` with tyutool 3.2.9. Handshake, flash-ID read,
unprotect, erase, write, protect, and reboot all succeeded, and both USB serial
interfaces returned. A post-cleanup watchface transfer/launch regression is
still pending; do not describe the cleaned image as hardware-confirmed until
that test completes.

Bluetooth should likewise be described as mostly stable, not fully stable.
Once a connection is established it commonly remains usable, including the
successful watchface run above, but connection setup occasionally fails to
reach a clean session. The unsuccessful path can also cause an AP reboot. This
intermittent connect/reboot defect remains open and requires failure-path
capture plus repeated connect, disconnect, reconnect, and cold-boot testing.
