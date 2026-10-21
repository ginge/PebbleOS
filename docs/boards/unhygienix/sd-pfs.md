# Optional TF-card PFS

The experimental `CONFIG_T5AI_SD_PFS` backend stores a 32 MiB PFS volume
on the TF card. Internal-flash PFS remains the default and is not migrated.
All production code is board/SoC/driver-local; linker wrapping selects the
backend without changing stock PFS. The wrapped call boundaries disable LTO
in the original PFS, FTL and filesystem-region translation units.

## Build

Use a separate build directory:

```sh
cmake -S . -B build-sd -G Ninja -DBOARD=unhygienix -DCONFIG_T5AI_SD_PFS=y
cmake --build build-sd
```

`CONFIG_T5AI_SD_4BIT=y` is the SD build default. Set it to `n` for a one-bit
DMA diagnostic build. Both modes identify at approximately 100 kHz and then
use the 320 MHz SD source divided by 16 (20 MHz). No high-speed card mode is
required. SDHC/SDXC and SDSC capacities/addressing are decoded separately;
SDUC and unsupported CSD versions are rejected.

## Ownership and logging

The AP owns the SDIO v2 controller, commands, transfers and DMA0 channels 2
(TX) and 3 (RX). Display channel 0, microphone channel 1 and CP logical
channels 12–15 retain their ownership. The board dispatches the shared GDMA
IRQ to all enabled AP clients. Audio shutdown cannot disable that shared IRQ
in an SD build. There is no new CP sector service or CP image patch.
The existing CP PM service votes SDIO/DMA0 power and enables the SD clock.

Waveshare schematic wiring:

| Signal | GPIO |
|---|---:|
| CLK | 2 |
| CMD | 3 |
| D0 | 4 |
| D1 | 5 |
| D2 | 10 |
| D3 | 11 |
| Card detect, active low | 8 |

GPIO10/11 are also UART0 on the programming USB interface (`if00`). Four-bit
SD prevents the AP UART driver from remapping these pins. Runtime logs already
use the CP mailbox and UART1 on the second onboard USB interface (`if02`,
460800 baud), so no external serial adapter is needed. Existing CP-shell
commands remain on that path; a general bidirectional Pebble console is still
unsupported. Boot-ROM programming remains a reset-time operation.

The onboard bridge also connects electrically to the shared UART0/SD pins.
Firmware muxing alone does not establish that the bridge TX is high impedance.
Four-bit operation must be qualified with the onboard bridge attached; compare
against one-bit DMA if identification works but data CRC fails. Do not claim
four-bit electrical coexistence or sustained performance from host tests.

## Card layout and destructive initialization

This is a private raw card area, not FAT and not a desktop-mountable partition.
Sector size is 512 bytes:

- Header: LBA 2048, magic `PBLSDPFS`, format version 1.
- PFS data: LBA 2176 through 67711 inclusive (exactly 32 MiB).
- Minimum card capacity: 67712 sectors (33.0625 MiB).

An unrecognized header causes the PFS area to be overwritten and formatted.
The valid header is written only after PFS initialization succeeds. The prefix,
header padding and trailing card sectors are outside the PFS erase range, but
ordinary desktop filesystems overlapping the private area will be destroyed.
Returning to an internal-flash build exposes its previous independent state.

PFS reserves 64 KiB for bookkeeping and applies its existing 80% capacity policy,
leaving approximately 25.55 MiB before allocated files. Page flags consume 8 KiB
of kernel heap. The adapter's 4 KiB sector cache and a shared aligned 4 KiB DMA workspace
use the existing private board PSRAM reservation. The recursive driver lock
covers each adapter read/modify/write, preventing raw block callers from
reusing the workspace between its read and write.

## Transfer and failure contract

The driver supports up to eight sectors per DMA request, aligned direct DMA
within AP-owned SRAM/private PSRAM and staging for other buffers. Cache maintenance
occurs before/after DMA. Transmit completion waits for FIFO drain after DMA;
DMA completion alone is insufficient. Multi-block reads and every write terminate with
CMD12, then CMD13 confirms the card is ready before success is returned.

The adapter caches small reads, batches aligned multi-sector operations and
implements NOR-style bit clearing by read/modify/write. Erase writes `0xff`;
SD discard/erase contents are not assumed. Writes are synchronous. There is no
write-back cache, migration, live-removal support or sector journal.

Command and transfer deadlines are bounded. Transfer faults stop both owned
DMA channels and quarantine the driver. The stock FTL cannot return errors,
so backend I/O errors assert rather than reporting successful filesystem writes.
A missing/undersized card fails initialization; there is no automatic NOR fallback.
This is an experimental fail-stop backend, not graceful removable-media support.

Power interruption may corrupt sectors and PFS. The contents are disposable;
reformat/reprovision when necessary. Existing PFS recovery and formatting do not
provide a general SD integrity checker or guarantee detection of every corruption.

## Validation

Run `python3 -m unittest tools.tests.test_t5ai_sd_pfs -v` for executable tests
against memory-backed media and an MMIO/interrupt model. These cover provisioning,
reopening, NOR semantics, partial/multi-sector transfers, cache invalidation,
boundaries, missing/undersized media, DMA error/timeout handling and isolation of
other DMA channels. They do not validate the electrical controller behavior.

Hardware qualification must verify actual received serial bytes, card identity,
four-bit operation, read/write CRCs, app install/launch/reboot, full-volume handling,
display/microphone concurrency and Bluetooth reconnect/idle soak. Record throughput,
CPU time and long-tail latency before calling the driver performance-qualified.

### Software validation, 2026-10-21

Both the default internal-flash build and the four-bit SD build completed,
including protected-image generation, image-layout validation, resource bounds,
log hashing and syscall checks. Disassembly confirmed that PFS initialization,
FTL operations and filesystem formatting select the wrappers in the LTO image.
The selected SD, console, display DMA, CP-manifest and packaging suites passed
22 tests. Ruff, new-C-source formatting and `git diff --check` passed.

| Link allocation | Internal PFS | SD PFS |
|---|---:|---:|
| Flash | 1,611,480 bytes | 1,614,928 bytes |
| Static kernel RAM | 105,664 bytes | 105,760 bytes |
| Private PSRAM | 122,432 bytes | 130,624 bytes |

The SD build has 21,216 bytes of linker-provided kernel heap before runtime
allocations. Its 8,192-byte page-flags allocation leaves 13,024 bytes before
other runtime allocations. This is not a measured runtime low-water mark.
The private PSRAM reservation has only 448 bytes unused; configurations adding
more private buffers must be checked separately.

### Hardware bring-up, 2026-10-21

The original AP slot was backed up before flashing SD firmware. Only the AP
firmware range is updated; CP, resources and the internal-flash PFS are preserved.
Live CP UART1 bytes confirmed that the SD candidate boots and reads the card.
Initial provisioning reached formatting but timed out on its first eight-sector
write. Diagnostics showed DMA completion followed by a CMD13 timeout.
The driver now follows the vendor write setup and stop sequence: issue CMD24/25
with the data engine reset, release the transmit engine afterward, and disable
TX-FIFO clock gating after CMD12 before status polling.

The download interface can require repeated handshake attempts or a physical
reset/power cycle after four-bit SD claims the shared pins. Runtime logging
continues over the second onboard USB interface without an external adapter.
The clock-gate correction completed a full 32 MiB format in approximately
94 seconds and subsequently reached the launcher. A later CMD24 single-sector
write timed out. The driver now uses the vendor's CMD25/multi-block data mode
plus CMD12 termination even for a one-sector write. The final correction was flashed to the AP-only physical range
`0x132000..0x2d4ea6` (end exclusive). Build ID
`ad810fffb0ef34136790ba98c1b19fd934744a72` booted successfully, reopened the
existing header (`new=0`) with four-bit SD and 61,962,240 reported sectors,
and found 47 apps. An explicit reset reproduced the same header, app count
and launcher startup. Both boots reconnected to the phone with an encrypted
BLE connection and opened a PPoGATT session. No SD I/O errors or assertions
were observed in the final-image capture. These are basic bring-up checks;
runtime heap headroom, bridge coexistence under sustained load, app launch,
full-volume handling and measured throughput remain unqualified. The final
capture later recorded a BLE connection timeout (`0x0208`) and a put-bytes
failure, without an SD I/O error or filesystem assertion. The cause has not
been established; sustained phone/app-transfer stability is not qualified.
