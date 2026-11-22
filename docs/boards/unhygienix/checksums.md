# T5AI checksum boundaries

These wire checksums belong to the pinned CP services. Similar names do not make
them interchangeable. No production algorithm is replaced by the evaluation
below, and no persisted/on-wire value or covered byte range changes.

| Consumer | Existing algorithm | `123456789` | Native API relationship |
| --- | --- | --- | --- |
| Flash IPC | Reflected polynomial `0xedb88320`, initial accumulator zero, no final XOR | `0x2dfd2d88` | For nonempty input, `~pbl_crc32(UINT32_MAX, data, size)` |
| SARADC IPC | XOR each byte into a **32-bit** zero accumulator; eight left shifts testing **bit 7**, XOR `0x31` when set; no byte mask | `0x47ffc2a2` | Neither native CRC-32 nor CRC-8; retain the exact vendor recurrence |
| MIPC | Reflected CRC-8 polynomial `0xab` (normal form `0xd5`), initial `0xff`, no final XOR | `0x7f` | Native `pbl_crc8` uses polynomial `0x2f`; reversing byte order does not make it equivalent |

The native `pbl_crc32` API complements its input and output state. The flash
adapter therefore complements both boundaries. Empty input needs an explicit
zero result: `pbl_crc32` returns zero for a NULL pointer, so blindly complementing
its result would give the wrong value for `(NULL, 0)`. Invalid nonempty NULL
buffers are not an admitted flash transaction and are not a new API contract.

MIPC also has a pinned checksum span. `t5ai_mipc_data_crc()` starts at `result`
and uses `sizeof(data) - sizeof(list) - sizeof(result)`: 20 bytes in the existing
32-byte layout. The list and final `flags` word are outside that span. Do not
replace it with a guessed whole-payload checksum. SARADC covers the raw sample
bytes (`count * sizeof(uint16_t)`); its checksum result must not be narrowed to
a byte, despite the bit-7 test and polynomial resembling CRC-8.

## Evaluation and regression checks

`tools/tests/t5ai_checksum_harness.py` compiles the complete production AP flash,
SARADC, MIPC and native CRC units, plus the pinned vendor `flash_server.c`,
`saradc_server.c` and `bk_crc.c`. Small wrappers expose their checksum entry
points; no function body is extracted and no vendor layout is copied. Actual
board Kconfig and production/vendor headers are used. A 32-bit host build keeps
the vendor `size_t` and pointer ABI and runs the existing SARADC layout assertions.
Unused firmware OS/hardware paths are removed by section GC. Hidden visibility
keeps sanitizer dynamic exports from retaining those unrelated entry points;
there are no fabricated OS/hardware implementations.

The harness verifies the vendor submodule revisions and clean tracked inputs.
`UNHYGIENIX_CRC_VENDOR_FLAGS` must point to a generated vendor CP driver's
`flags.make` with `C_INCLUDES`. SDK include paths are redirected to the pinned
production SDK; the generated CP configuration remains from that build. The
host requires Clang 21, 32-bit libc and 32-bit ASan/UBSan runtimes, plus the
existing Python test dependencies. A missing flag path produces an explicit
optional-test skip; compilation failures with it supplied are failures.

```
UNHYGIENIX_CRC_VENDOR_FLAGS=/path/to/armino/driver/CMakeFiles/__armino_driver.dir/flags.make \
ASAN_OPTIONS=detect_leaks=0 \
python -m unittest tools.tests.test_t5ai_checksums -v
```

Vectors compare AP, complete vendor code and the evaluation-only native adapter:
all lengths 0–2048, alignments 0–3, four payload patterns, boundaries through
65,535 bytes and every single-byte value. SARADC comparisons cover lengths 0–64
at all four alignments/patterns. MIPC covers all 256 initial states, lengths
0–128, and its actual production data-structure span. Fixed vectors above guard
against silently changing both sides together. All compared code and the vector
fixture are compiled with ASan/UBSan in the checked configuration.

The same fixture has an optional unsanitized `bench` mode. It measures five
trials per size over nontrivial payload bytes, taking median nanoseconds per
call with a volatile result sink. It has no performance pass/fail threshold:
host scheduling, frequency and cache behavior do not qualify watch timing.

## Current decision — 22 November 2026

Retain the 256-entry flash table. In the recorded Clang 21, 32-bit x86 `-O2`
host run, the native nibble-table adapter takes about **2.0–2.1 times** the AP
byte-table time for 256–65,535-byte payloads, and **3.3 times** for 32 bytes.
This is a warning about possible cost, not a BK7258 cycle measurement. The
stock CP read server's 50-tick lease and flash client ownership/quarantine rules
remain unchanged. No timing limit is relaxed to accommodate an adapter.

Before removing the table, measure the real target build and cache placement
under the flash read lease and representative traffic. Preserve first failures,
verify vector equality and check read/lease/priority behavior on the watch. The
current host result does not justify accepting that runtime change merely to
save the 1,024-byte table. Stock/shared CRC code also remains untouched.

Evidence and exact commands are recorded in the [quality TODO](code-quality-todo.md),
under `build-quality-20261121/checksum-finding20a/`. The separate optional Tuya
application/reference comparison work remains the next part of finding 20.
