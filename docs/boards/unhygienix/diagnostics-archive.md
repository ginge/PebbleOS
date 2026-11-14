# Retired QSPI/DMA diagnostics

The QSPI/DMA recorder and pre-splash mixed-I/O/display-energy workloads were
removed from production on 14 November 2026 at the operator's request. Their
source, tests, measurements and first-failure captures remain available.
DMA clock-policy inheritance stays **enabled by default**. Its reset-release
guard, no shared-global writes and explicit opt-out remain production behavior.

## What was removed and retained

Removed three SoC modules, their three headers, three diagnostic/harness test
modules, three Kconfig options, CMake entries and hooks in DMA, SPI, board startup
and AP sleep. Production no longer includes T5QD calls, snapshots, counters,
profile reports, test patterns, microphone-discard or test SD reads. No disabled
instrumentation stubs remain in those paths. Unrelated older diagnostics were
outside this scoped cleanup.

Keep actual failure handling: QSPI power/pin/device/config acquisition failures,
transfer deadlines and final wire drain, cyclic owner retention, cancellation,
DMA reset-held rejection, storage completion and retained blockers. PSRAM power
acknowledgement now always precedes both validated ranges and framebuffer use,
without a profiling conditional. Functional mailbox/QSPI failure regressions live
in the ordinary SPI tests; board PSRAM readiness and DMA ownership/policy remain
actual-source tests. No shared Pebble or CP code/policy changes.

## Exact source archive

Reference workspace archive:

`/home/baz/Documents/ChatGPT/pebble/artifacts/diagnostics-retirement-20261114/`

- `before/`: exact 18 pre-retirement source/integration/test files and SHA256 map.
- `restore-diagnostics.patch`: unapplied patch from the cleaned source to that
  exact implementation, including the previous integration/test layout.
- `restore-validation.json`: check against the cleaned production tree, then
  apply in a temporary tree and compare every restored byte with the archive.
- `normal/`, `tests*.log`, `result.json`, `RESULTS.md`: cleanup validation and
  installed-state limits. Earlier trial artifacts are unchanged.

Restore patch SHA256:
`0ab1371ef3def39bac6cbfbd2314682641cbc3dda8381622d8f4934d60dbe3db`.

## Reuse later

From production `/home/baz/coredevices/pebbleos`, inspect the archive and use a
separate reviewable checkout if production has evolved. Verify the hash, then:

```sh
git apply --check /home/baz/Documents/ChatGPT/pebble/artifacts/diagnostics-retirement-20261114/restore-diagnostics.patch
git apply /home/baz/Documents/ChatGPT/pebble/artifacts/diagnostics-retirement-20261114/restore-diagnostics.patch
```

The patch restores code and integration; it does not enable test workloads.
Review any newer production safety changes before applying an older snapshot.
Enable `T5AI_QSPI_DMA_PROFILE` for bounded boot/32-return reports. Select at most
one of `T5AI_IO_QUALIFICATION` or `T5AI_DISPLAY_ENERGY_TEST`, deliberately, for a
prepared hardware trial. The exact prior method and limits remain documented in
[diagnostic history](qspi-dma-diagnostics.md), [DMA policy](dma-clock-policy.md)
and [fixed-workload energy history](display-energy.md).

Rerun restored actual-source tests, effective configuration, CRC/map/resources,
heap/cache and actual linked SRAM-WFI audits before flashing. Preserve rollback
and first failure. Recheck by-id UART enumeration and actual bytes. Reconfirm
USB/battery/UART wiring and obtain the operator's requested test conditions;
no shutdown or overnight permission follows from restoring diagnostic code.

The display-energy harness must retain explicit boundary flushing: its first
control failed timing acceptance due to partial PULSE frames held until the next
phase. Apply only completed-workload energy boundaries with matched AP/radio
state and documented transport/meter uncertainty. CP channel registers remain
unproven; do not restore unsafe early PSRAM access or claim global gate ownership.

## Existing CMake caches

The retired options no longer exist in active Kconfig. Remove cached overrides
for `CONFIG_T5AI_QSPI_DMA_PROFILE`, `CONFIG_T5AI_IO_QUALIFICATION` and
`CONFIG_T5AI_DISPLAY_ENERGY_TEST` with CMake `-U` flags (or use a fresh build).
Do not replay old artifact build scripts against production without accounting
for their removed options. After restoring the patch, those options exist again.
