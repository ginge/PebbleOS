# Unhygienix Bluetooth RF PLL initialization hook

> Current project status: [22 October update](../../../../docs/boards/unhygienix/current-status.md). The recorder and zero-latency workaround remain archived; later radio timeouts are still open.

`ble_rf_init.c` calls the vendor RF configurator with `0x0202` immediately after
successful Bluetooth initialization in the CP HCI-reset thread. The hook calls
the original initializer first and leaves RF unchanged if initialization fails.

`tools/patch_unhygienix_cp_rf.py` verifies the complete pinned stock image hash
and reset function, compiles the hook into unused CP partition space and redirects
only the existing Bluetooth-init call. Existing addresses and the AP mailbox/HCI
ABI stay unchanged. Unknown images and oversized hooks are rejected.
This is source-hook integration into a pinned binary, not a full CP rebuild.

Only RF initialization and the interval-transition correction are supported. The optional CP flash guard, volatile
protection and SPI bypass sources, packaging options and tests were removed on
2026-10-18. Historical experiments remain in Git and the preserved evidence;
CP manifests containing flash guard metadata are rejected. Rebuild a Bluetooth-only
CP image before using the current runner with an old diagnostic artifact.

Run from the repository root, with the stock image supplied under `build/inputs/`:

```sh
pbl configure --board unhygienix -DT5AI_CP_STOCK="$PWD/build/inputs/stock-cp.bin"
pbl build t5ai_cp
pbl flash --dry-run --t5ai-cp --tyutool tyutool_cli --tyutool-tty /dev/serial/by-id/programming-uart
```

The native runner updates AP and explicitly selected CP, preserving PFS.
Remove `--dry-run` to program. For CP-only rollback, regenerate a protected stock
payload and use `tools/flash_unhygienix_cp_firmware.py`; it validates by default
and writes only with `--flash`. AP, resources, PFS and bonds remain outside its
write range. Wi-Fi coexistence requires separate validation of this radio policy.

See the [tooling guide](../../../../docs/boards/unhygienix/tooling.md) for pinned
inputs and provisioning, and the [operator handoff](../../../../docs/boards/unhygienix/index.md)
for hardware qualification requirements.

## Connection-interval latency correction

`ble_latency_fix.c` retains the vendor max-latency calculation, then clears
stale `latency_applied` state at both connection-parameter application sites
in the pinned controller scheduler. Pending timestamps and event increments
were calculated with the old interval. Keep them paired until event start;
transmit-time rewind must not subtract the new interval from that schedule.
Normal peripheral latency and early wake outside parameter transitions remain
available. Shared Pebble connection policy is unchanged.

Normal `t5ai_cp` packaging includes this correction alongside the RF hook.
`tools/patch_unhygienix_cp_latency.py` also packages a fix-only image from the
pinned stock CP or the exact verified installed baseline used in the hardware
investigation, preserving existing RF/flash-service hooks in the latter.
It guards the complete scheduler and transmit functions, both original calls,
and partition bounds. The standalone manifest binds base, fix source and
output hashes. There is no AP buffer, tracing hook or paired-AP dependency.

`tools.tests.test_t5ai_cp_latency_arm` optionally executes the actual vendor
ARM scheduler, transmit rewind and compiled correction with Unicorn. Set
`PBL_CP_LATENCY_BASE` and `PBL_CP_LATENCY_FIXED` to reviewed baseline and
fixed binaries. Tests reproduce the original failure, check both update
paths and event-counter consumption, and preserve normal latency rewind.
Radio helpers are stubbed; hardware validation is separate. Two Pebblecraft
transfers passed on 2026-10-21, including confirmed latency30 to latency0.
Long soak and energy impact remain unqualified. The vendor latency cap's
separate event-counter-wrap behavior is preserved.

The diagnostic recorder, its build flags and zero-latency workaround were
removed from production source. Their source, exact paired firmware, tests,
manifests and captures are archived outside this repository under
`/home/baz/Documents/ChatGPT/pebble/artifacts/bt-latency-recorder-20261021/`.

## Later radio qualification — 2026-10-22

The AP-only SD heap correction did not modify CP. Its resumed 25.56-minute
capture recorded seven supervision-timeout disconnects (`0x208`); all reconnected
and reopened the phone session without a watch reboot. The two successful
October 21 transfer tests remain valid bounded evidence, not complete radio
qualification. Sustained transfers, reconnect churn, cold boot, long soak and
energy impact remain open. [Evidence](/home/baz/Documents/ChatGPT/pebble/artifacts/sd-pfs-heap-fix-20261022/results.txt).
