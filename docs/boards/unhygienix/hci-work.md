# T5AI HCI work and wait boundaries

Finding 9 enables `CONFIG_T5AI_HCI_EVENT_WAIT` by default on Unhygienix,
with or without `T5AI_POWER_SAVINGS`, at the operator's request on 22 November.
Explicitly setting it to `n` restores the existing 5 ms busy / 100 ms quiet
fallback bounds. Every configuration now uses the same
latched upper-task notification, including configurations without retained sleep.
No shared NimBLE, kernel or mailbox code is changed.

## Producers and consumers

The upper `T5AIHCI` task sends queued host ACL packets and schedules host reset
on transport failure. The higher-priority `T5AI HCI IPC` task drains snapshotted
controller frames, delivers host callbacks and retires transport ownership.
Their binary semaphores are separate; a give before take remains latched.

| Producer | Publication and signal | Consumer / action |
| --- | --- | --- |
| Host ACL enqueue | `ble_transport_to_ll_acl_impl`: successful message-queue put, release TX mutex, `t5ai_hci_notify_work` | Upper task dequeues and sends the packet; failed enqueue still consumes/frees the packet under its existing contract. |
| Controller event, ACL, exchange response or transport ACK | Mailbox dispatcher calls `prv_route_frame`: copy frame, acknowledge physical snapshot, queue pending frame, give `s_worker_wake` and notify upper task | IPC task drains all queued frames. Reliable RX is copied before CP can reuse its buffer; ACKs retire only matching owned descriptors. |
| Queue overflow, bad descriptor, receive deadline or terminal exchange failure | `prv_mark_faulted`: publish fault / stopped state, notify upper task | Upper task schedules the existing NimBLE host reset. A late fault gets its own notification after any earlier frame notification. |
| Matching synchronous command / ACL exchange response | IPC task gives `s_exchange_ready` after handling the matching subtype | Original sender resumes its bounded exchange. This is independent of both work semaphores. |
| Retained event / ACL cannot allocate a host buffer | IPC task owns the AP copy and its original received tick | Candidate IPC wait retries in 5 ms, capped by the oldest packet's remaining 250 ms lifetime. It stops timing retries when the FIFO empties. |
| HCI / flash profiling report | Upper task's previous report tick | Candidate waits only until the existing 30 s report deadline, while any work notification can wake it sooner. |
| Start / stop / recovery | Existing synchronous exchanges and explicit IPC-worker kicks | Route/ACK signals service exchanges independently of the upper task. Bounded ownership drain and descriptor quarantine rules stay intact. |
| Whole-chip reset request | Existing asynchronous system request | No normal HCI retry is expected after this request; the existing sleep veto and reset behavior remain unchanged. |

Sources: [upper transport](../../../third_party/nimble/transport/hci_t5ai.c),
[IPC transport](../../../third_party/nimble/transport/t5ai_hci_ipc.c),
[physical dispatcher](../../../drivers/t5ai/cp_mailbox.c), and
[interface](../../../include/pbl/soc/t5ai/hci_ipc.h).

NimBLE provides an ACL-pool put callback, but its event-pool free path has no
matching general notification. A reliable retained event therefore still needs
an actual capacity-retry deadline. The candidate uses the existing 5 ms retry
cadence locally while a packet is retained; it does not add wrappers or edits to
shared pool/free code. Incoming frames can also retry retained packets early.
All elapsed comparisons against the stored 32-bit received tick now use that
same width, preserving the 250 ms lifetime across uptime rollover.

## Wait policies and remaining qualification

| State | Explicit polling fallback | Default event waits |
| --- | --- | --- |
| Upper task idle, retained sleep enabled | Notification or 100 ms timeout when IPC is quiet; otherwise 5 ms | Notification; no timer unless a profile report is due |
| Upper task idle, retained sleep disabled | Notification or existing 5 ms timeout | Notification; no timer unless a profile report is due |
| IPC task, no retained host packet | Existing work semaphore, woken also by upper polling | Work semaphore; no idle timeout |
| IPC task, retained host packet | Upper polling / new incoming frames trigger retries | Work semaphore or min(5 ms, remaining receive lifetime) |
| ACL rejected / unpublished busy | Existing 5 ms retry, bounded by original 2500 ms packet deadline | Same |
| Physical send busy / start-stop ownership drain | Existing bounded 1 ms retries and 50/250 ms drain limits | Same |

The candidate changes persistent worker idle waits. Synchronous send/recovery
waits retain their existing retry deadlines and ownership rules; they do not run
periodically when there is no work. An outstanding CP-owned descriptor alone is
not a reason to repeatedly wake either worker or reclaim storage: only its ACK
or the existing fault/recovery path can retire it.

Host models compare a 60-second quiet interval: 600 upper timer wakes with the
retained fallback, 12000 without retained sleep, and zero with the candidate
when profiling is off. A notification before or at blocking is consumed in the
same modeled tick. These figures exercise the actual wait selector, not physical
watch scheduling, packet latency, radio traffic or power consumption.

Before changing the default, compare matched baseline/candidate device builds
under connected idle and representative event/ACL traffic. Record actual worker
wake counts, packet dispatch latency, capacity pressure, reconnect/reset and
ownership/quarantine behavior. Include profiling deadlines in the comparison;
profiling itself wakes the upper task at most every 30 seconds when quiet.
Host success and firmware packaging do not supply this device qualification.

## Reproduce host validation

Use the existing development Python environment, Clang 21, Ruff and CMake on
PATH. The full-unit tests include real kernel types/time conversion, NimBLE
mbuf handling and MIPC definitions; endpoint scheduling and CP traffic are
modeled. ASan/UBSan should run outside the restricted sandbox on this host.

```sh
python -m unittest tools.tests.test_t5ai_hci_work \
  tools.tests.test_t5ai_hci_sleep tools.tests.test_t5ai_hci_acl \
  tools.tests.test_t5ai_hci_pressure tools.tests.test_t5ai_native_objects \
  tools.tests.test_t5ai_config
```

The full-unit matrix checks retained sleep on/off, event waits on/off and HCI
and flash profiling individually or together. Tests require the requested Kconfig values to take effect.
It covers coalesced and racing notifications, enqueue ownership, actual IPC
worker frame drain, failed allocation retry, the remaining one-tick deadline,
receive expiry and 32-bit tick rollover. Profile builds also use the current
`PebbleTask_SysWorkq` enum; no shared task enum is changed.


## Phone-assisted device attempt, 22 November 2026

Matched profile builds A `4762101d711f74b2f6db465cb9c07a520502c219` and
B `76b2ff008a6341fc4d70605fbb7d90d778d92aa5` differ only in event waits.
Profile-only semaphore-return counters measure actual worker signals/timeouts
without adding a timer or changing a protocol/retained layout. The normal
profiling-off build has no counter storage or report.

Fallback A completed connected idle, 2,200 echoes, three 67,600-byte transfers
and 25 verified phone Bluetooth reconnect cycles. Its sampled idle interval
recorded 5,699 upper timeout wakes over 570.267 s. Two candidate startup attempts
failed the 90 s session-ready limit; a warm attempt had GATT/session failures.
A final fallback restoration also failed the same startup limit, and historical
normal controls have discovery failures. These data do not establish causation
or qualify candidate latency/throughput. Both original and recovered attempts
are retained; event waits stay off by default.

See the [TODO progress record](code-quality-todo.md) for commands, control
variability, failed tooling, remaining tests and the exact installed fallback.
Evidence: `build-quality-20261121/hci-work-finding9/ab-20261122/`.

## Operator acceptance, 22 November 2026

The operator accepts finding 9 as qualified under the working assumption that
auto-reconnect failures are phone-side. Host/build validation and the device
observations above support the accepted implementation; they do not establish
a phone-only root cause or a completed candidate latency/throughput comparison.
Original failures and the sealed measured report remain unchanged. This
acceptance closes finding 9 without changing the default or installed fallback.
The matched device comparison remains required before enabling event waits by
default. The implementation remains uncommitted; finding 10 is next.

## Default enabled at operator request, 22 November 2026

Following operator acceptance, event/deadline waits are now the board default.
HCI profiling remains off by default; `CONFIG_T5AI_HCI_EVENT_WAIT=n` retains the
fallback for comparison or recovery. This supersedes the earlier default-change
hold recorded above, without adding a completed connected candidate A/B result.
The installed fallback image is unchanged by this source change.

The operator subsequently requested committing the finding-9 implementation,
enabled default, tests and this record. The default-on ARM/LTO build and all
23 relevant host tests passed; no flash accompanies the commit.
