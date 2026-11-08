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
