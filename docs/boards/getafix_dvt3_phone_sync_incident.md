# Getafix DVT3 phone-sync incident: 2026-09-14 through 2026-09-15

## Paused checkpoint: end of 2026-09-15

Work is intentionally paused. Do not infer the current state from the older
chronological notes below without reading this checkpoint first.

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
/home/baz/Documents/ChatGPT/pebble/build/t5ai-15ms-sync.log
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
/home/baz/Documents/ChatGPT/pebble/build/t5ai-inplace-ack-sync.log
/home/baz/Documents/ChatGPT/pebble/build/t5ai-inplace-ack-sync-mailbox.raw
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
4. Flash only the guarded AP image using the documented Getafix DVT3 AP-only
   procedure. Do not overwrite the bootloader, CP firmware, resources, bond
   data, or PFS.
5. Start a fixed-output trace with idle reconnect disabled before connecting
   the phone:

   ```sh
   .venv/bin/python tools/t5ai_trace.py --wait 0 --reconnect-idle 0 \
     --output build/traces/t5ai-conn-result-sync.log
   ```

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
`/home/baz/Documents/ChatGPT/pebble/build/t5ai-watchface-retained-trace.log`.

For the next attempt, start `tools/t5ai_trace.py` with idle reconnect disabled,
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
  `/home/baz/Documents/ChatGPT/pebble/build/t5ai-watchface-failure-mailbox.raw`;
- post-failure decoded PULSE capture (no new streamed messages):
  `/home/baz/Documents/ChatGPT/pebble/build/t5ai-watchface-failure-after.log`.

A second sync attempt reproduced the same CP MIPC CRC failure continuously.
Its raw capture is
`/home/baz/Documents/ChatGPT/pebble/build/t5ai-watchface-failure-2-mailbox.raw`.
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
- `src/fw/board/boards/board_t5ai.c`.

The shared event loop is stock and the app remains at its stock priority 1.
Any T5AI fix should stay in the board, SoC, mailbox, flash, or transport layer.
The earlier task-priority experiment was reverted and must not be used as a
phone-sync fix.

## Trace-tool issue

Use the permanent tracer documented in the board guide:

```sh
.venv/bin/python tools/t5ai_trace.py --wait 0 --output build/traces/NAME.log
```

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
   and immediately start `tools/t5ai_trace.py --reconnect-idle 0`.
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
