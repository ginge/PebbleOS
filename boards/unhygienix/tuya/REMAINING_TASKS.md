# Unhygienix validation and remaining work

Production source is this PebbleOS tree. Tuya material is a pinned reference and
isolated CP build input. Preserve Gabbro/Getafix-DVT2 protocol compatibility,
calibration, Bluetooth identity and stock shared code. The source cleanup does
not promote an unqualified image to a hardware release.

The authoritative active lists are:

- [Current installed firmware, evidence and release blockers](../../../docs/boards/unhygienix/current-status.md)
- [Ordered power TODO](../../../docs/boards/unhygienix/power-todo.md)
- [CP-awake AP sleep contract and build profile](../../../docs/boards/unhygienix/ap-only-sleep.md)
- [Build and flash procedures](../../../docs/boards/unhygienix/tooling.md)
- [Preserved investigation history](../../../docs/boards/unhygienix/power-investigation-history.md)

## Sensor increment completed — 13 November 2026

Normal AP-only cleanup build `4545e34ad182f3f80411b93f46c2dbdf364a9e5f` is
installed with quiet-period WOM enabled by default. Configuration/rollback,
direct/FIFO/WOM acquisition and diagnostics are separated; redundant configuration,
rotation register I/O outside WOM and unnecessary FIFO decoding are removed.
Eleven targeted sanitizer tests and ARM variants pass; linked text -48 B,
data/BSS unchanged. Clean boot, 32 retained AP sleep returns, encrypted phone
recovery and final operator first-wake/touch/button checks pass. CP frequency,
closed settings, resources/PFS/bonds and shared code are unchanged.

Broader gesture latency/sensitivity, client/concurrent-load, electrical and
long-term qualification remain open. Streaming-watchdog backoff is a separate
future experiment. See the [sensor review](../../../docs/boards/unhygienix/sensor-power-review.md)
and [driver guide](../../../docs/boards/unhygienix/qmi8658_acquisition.md).

## CP/AP deployment gates

- [x] Commit the layout, pinned alarm-server ABI/build recipe and default-on AP consumer in dependency order.
- [ ] Keep exact CP payload/config/controller/calibration hashes and source provenance reproducible.
- [ ] Require actual readback/CRCs and expected AP boot after any paired deployment.
- [ ] Preserve AP layout, resource boundaries and existing PFS; never format to hide a storage failure.
- [ ] Preserve original and known-normal rollback images and first-failure evidence.
- [ ] Resolve discovery/parameter-update timeouts and uncommanded Bluetooth drops.
- [ ] Repeat app/watchface install/launch, 25 consecutive reconnects, full cold bond/face restoration and deferred overnight interaction.
- [ ] Verify mixed MIC/SD/display ownership, timer/watchdog/heap integrity and durable writes.
- [ ] Measure matched idle and completed-workload energy before claiming savings or release readiness.

Production retained idle keeps CP awake. CP low-voltage sleep is deferred and its
failed experiments remain bounded diagnostics. The selected image retains the
earlier dim display policy. No overnight qualification is running or recorded
as passed. Historical transient installed-state statements are preserved in the
history documents and are superseded by current-status.md.
