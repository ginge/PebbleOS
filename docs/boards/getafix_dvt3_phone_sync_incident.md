# Getafix DVT3 phone-sync incident: 2026-09-14

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

The T5AI-local connection policy currently treats the controller-established
15 ms, zero-latency link as satisfying the logical responsiveness request and
uses a 20-second supervision timeout. This avoids a controller failure seen
when requesting the stock idle policy, but increases connected radio duty
cycle and delays genuine link-loss detection. It is an explicit temporary
power compromise. Restore stock connection parameters only after sustained
sync is reliable, then measure connected-idle current and battery life.

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
