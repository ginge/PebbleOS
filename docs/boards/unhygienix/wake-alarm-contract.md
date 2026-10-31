# Unhygienix wake-alarm contract review — 31 October 2026

This is source evidence and an implementation dependency, not a new wake driver
or hardware qualification. Keep the application sleep veto and the current tick
policy. No AON compare, wake source, display state or installed firmware is changed
by this review. Wi-Fi qualification runs against its frozen AP/CP separately.

## The pinned wake-config reply is earlier than alarm installation

In pinned T5AI `t5_os/cp/middleware/driver/pwr_clk/low_pwr_core.c`, the
`LOW_PWR_CORE_WAKEUP_SRC_CFG` case sends `PM_WAKEUP_CONFIG_CMD` success **before**
calling `low_pwr_core_wakeup_src_cfg_handle()`. The RTC handler then dereferences
`param3` as `pm_rtc_wakeup_config_t *`, unregisters `lv_rtc`, calls
`bk_alarm_register(AON_RTC_ID_1, ...)`, and enables the wake source. Neither alarm
return value is checked; the wrapper also discards the handler's result.

Thus the logical reply is neither proof that the two-word payload was consumed
nor proof that a wake alarm was armed. A stack-local payload or immediate reuse
following that reply can race CP's later read. Mailbox physical completion also
does not prove alarm installation. The reference AP keeps `s_pm_rtc_config`
static, but its public configuration API forwards a caller pointer. Static
storage alone does not resolve replacement/cancellation races.

Both pinned alarm implementations have actual registration failures (invalid
ID/pointer, minimum lead period, exhausted nodes). The 32-bit driver rejects a
zero period count after requesting a node; the reference AP's unregister helper
sends period 1000/count 0, relying on the preceding `lv_rtc` unregister rather
than an explicit cancellation result. Propagate and test real outcomes instead
of translating the early reply into success.

## Wake delivery and ownership are separate from request completion

The RTC callback sends `PM_SLEEP_WAKEUP_NOTIFY_CMD` (`0x10`) with low-voltage
mode and RTC source. That event carries no AP generation/deadline identity. A
GPIO event shares the command with a different source. The CP low-power worker
also uses these events to restore AP power when needed; a notification is not
by itself proof of retained native-kernel resumption.

Native `drivers/t5ai/cp_mailbox.c` currently queues PM messages on `0x42` for a
blocking request consumer. `prv_pm_request()` drains old queued messages and
expects the next logical reply to match its command. It has no asynchronous
wake-event consumer. A wake can therefore be discarded by the next request's
drain or mistaken for that request's response. Existing shared-buffer bounds,
cache handling, transaction quarantine and heartbeat behavior must survive any
new routing. CP owns the alarm list/compare; do not write its compare from AP.

## Required next implementation and validation

1. Add a board-owned CP alarm operation that returns **after** configuration or
   cancellation is applied, with real registration status and an explicit owner
   and generation. Keep any vendor changes in isolated build overlays; preserve
   the production submodules and legacy CP guards. Validate the resulting new
   source/config/image hashes as a new candidate, separate from the current
   Wi-Fi-only power trial.
2. Hold the payload in validated, aligned shared storage with explicit cache and
   lifetime ownership through the final consumption result. On timeout retain
   or quarantine that storage; do not reuse it based on a physical ACK. Validate
   range, period/count multiplication, minimum lead and deadline wrap before CP
   modifies an alarm. Preserve other CP alarm owners and radio supervision.
3. Route wake notifications independently from synchronous PM replies. Define
   cancel-versus-fire, replacement, stale/duplicate notifications, queue-full
   handling, callback execution context and wake-before-idle races. An unrelated
   wake must not satisfy a PM request or reset its timeout. An old event must not
   release a new generation's sleep contract.
4. Exercise the actual CP queue/RTC path with rejected registration, allocation
   failure, delayed consumption after the old early-ACK point, cancel/fire and
   replacement interleavings. Test native PM reply/event interleaving, cache
   bounds, stale generations, timeout quarantine and heartbeat progress. Keep
   SysTick and the application veto while qualifying physical wake delivery,
   lead-time distribution, early interrupts and pending wakes.
5. Only after that contract is proven may retained idle atomically arm the next
   deadline and release the relevant veto. Restore interrupt routes, peripheral
   state and exactly-once AON time accounting before resuming work. The opt-in
   ordinary-WFI timer experiment is a different, bounded contract and does not
   qualify retained sleep. nRF deadline selection and SiFli blocker accounting
   remain architectural references; do not copy their registers or RTOS calls.

Evidence is pinned source inspection, not measured alarm latency or electrical
savings. Normal AP flash has 328 bytes free in the frozen control; assess the
validated release image or a board-local size solution before adding this runtime
contract. Sensor, memory, CPU/radio and final display phases remain incomplete.

Host evidence now executes the pinned RTC handler and exact wake-config switch
case using device-call shims: ACK occurs before unregister/register, rejected
registration still reports success, and a payload mutation at ACK changes the
period consumed afterward. Source and generated-shim hashes and output are in
`artifacts/wifi-off-implementation-20261031/pinned-alarm-contract-check.json`
in the reference workspace. These are host ordering checks, not wake timing or
hardware qualification.
