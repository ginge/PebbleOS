# Unhygienix board instructions

## Unhygienix hardware target

- **Unhygienix** means **"The village fishmonger."** The build target is
  `unhygienix` and its board symbol is `CONFIG_BOARD_UNHYGIENIX`.
- This is the Waveshare T5-E1 Touch AMOLED 1.75 / Tuya T5AI (BK7258) hardware,
  selected with `unhygienix`; use
  `pbl configure --board unhygienix` followed by `pbl build`.
- Getafix DVT and DVT2 remain Pebble Round 2 hardware targets. Unhygienix is an
  independent hardware target; do not reintroduce Getafix board flags or place
  its Tuya code under `boards/getafix`.
- Preserve companion compatibility: retain `CONFIG_PLATFORM_GABBRO`, the
  Gabbro app ABI, and the existing Round 2 / Getafix DVT2 protocol identity
  (`FirmwareMetadataPlatformPebbleGetafixDVT2`). The protocol-only recovery
  metadata record does not imply a real Pebble recovery partition.
- Board configuration and Tuya reference/CP material belong under
  `boards/unhygienix/`; production T5AI code stays in the existing board,
  SoC and driver locations. The optional Tuya application is reference
  material, not the production PebbleOS kernel.
- Board implementation and declarations use `fw/board/boards/board_unhygienix.c`
  and `board_unhygienix.h`; generic T5AI types remain in `board/board_t5ai.h`.
  Board tooling uses `tools/unhygienix_*`, `flash_unhygienix_*`,
  `patch_unhygienix_cp_rf.py` and `check_unhygienix_image_map.py`.
  Generic protected-block encoding stays in `tools/t5ai_image.py`; board
  partition geometry and packaging belong in `tools/unhygienix_image.py`.
  Display data and splash remain shared with Getafix; do not duplicate them.
- The migration changed plumbing only. Preserve hardware behavior, pin maps,
  timing, flash/PFS layout, mailbox/HCI ABI and pinned vendor/CP inputs.
- The runner is `tyutool`. Existing recovery, OTA, PBZ bundle, bulk erase and
  unsupported raw-flash operations remain disabled. CP manifests must record
  `unhygienix` and be regenerated from the hash-verified stock CP input;
  do not relabel manifests to bypass validation.
See [operator handoff](../../docs/boards/unhygienix/index.md) and
[build/flash tooling](../../docs/boards/unhygienix/tooling.md) for current procedures.

## Current soak-test plan

The recommended validation sequence for the Unhygienix port is:

1. Repeat the post-cleanup watchface install and launch regression test.
2. Run 25 disconnect/reconnect cycles and preserve the first failed-session
   evidence.
3. Cold boot the watch and verify bond restoration and reconnection.
4. Run an overnight connected-idle soak with periodic UI interaction.

During the run, watch for and record:

- hard faults, assertions, uncommanded AP resets, and boot-reason changes;
- watchfaces reverting, failing to remain selected, or failing after reboot;
- unexpected disconnects or failed protocol-session establishment;
- mailbox, PFS/flash, AppFetch, BlobDB, and resource-storage errors;
- heap-low-water changes and any other behavioral or display oddities.

Any externally triggered AP reboot is a release blocker. Retain the complete
first-failure trace rather than overwriting it with a later successful run.
