# Unhygienix validation and remaining work

The [operator handoff](../../../docs/boards/unhygienix/index.md) records current hardware
support and implementation gaps. The [tooling guide](../../../docs/boards/unhygienix/tooling.md)
records dependency, build, packaging and flash procedures.

1. Verify watchface install/launch and selection persistence.
2. Run 25 disconnect/reconnect cycles, retaining the first complete failure trace.
3. Cold boot and verify bond restoration, protocol reconnection and selected face.
4. Run overnight connected-idle with periodic UI interaction.
5. Resolve App privileged-code stalls and resource/app-entry warnings.
6. Complete hardware wake/alarm routing and low-power operation.
7. Validate battery/charging with a fitted cell, RTC drift, privilege isolation,
   storage interruption and corruption recovery.
8. Define rollback and recovery before enabling firmware updates or OTA.

Keep captures and matching firmware/log dictionaries under `build/traces/`.
Host builds do not establish physical qualification. Confirm actual received
bytes before reporting a capture or soak as active.
