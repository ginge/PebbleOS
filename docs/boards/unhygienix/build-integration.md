# Unhygienix build integration

`soc/t5ai/CMakeLists.txt` lists the native SoC implementation, memory scripts,
assembly roots and size policy. `cmake/production.cmake` contains production
integration; `cmake/diagnostics.cmake` contains optional instrumentation. Paths
below are relative to the repository root. No shared C or CMake source is edited
by these selections.

## Explicit local backends

`cmake/backends.cmake` selects these implementations after the ordinary targets
exist. It removes exactly one stock source from its owning target and requires
exactly one local implementation in `soc__t5ai`. Missing/renamed targets, missing
or duplicate stock sources, and missing/duplicate local backends stop configure.
Other target sources and source-file properties remain intact.

| Stock source | Local backend | Reason |
| --- | --- | --- |
| `pebble/process_management/process_regions.c` | `soc/t5ai/process_regions.c` | STAR-MC1 app/worker memory must be non-shareable for caching; keep the pinned region addresses and permissions. |
| `subsys/logging/backends/flash_storage.c` when the flash backend is enabled | `soc/t5ai/log_storage.c` | No validated NOR log partition exists; preserve the existing unsupported-storage behavior without touching storage. |

The SD FTL and its page-flags storage already use the explicit
`FS_PFS_CUSTOM_FTL` / `FS_PFS_CUSTOM_PAGE_FLAGS` selections. No allocator or
`ftl_*` linker interception is used.

## Production interception inventory

These boundaries preserve existing behavior while keeping changes local.
Replacing them with shared extension APIs requires a separate justification;
graphics eligibility/performance review remains backlog finding 10.

| Boundary | Mechanism and purpose |
| --- | --- |
| Touch transport | Wrap `event_put`, `event_take_timeout`, `pbl_msgq_put`, `pbl_msgq_get`, and `recognizer_manager_handle_touch_event`. Keep events, msgq and recognizer manager outside LTO so public calls cross the owned-sample boundary. Rename `sys_get_ticks` only in pan recognition to preserve sample-time velocity. |
| SD PFS initialization | Wrap `pfs_init`; keep its stock translation unit outside LTO. Preserve pre/post-init header validation and publish the custom provider only after initialization. |
| CP console locks | Wrap `pbl_console_lock` / `pbl_console_unlock` and keep console.c outside LTO. Exclude mailbox-owner recursion before locking. Configure these rules only when `CONSOLE` is enabled; release normally disables that directory. |
| Shared log state | Rename `kernel_applib_get_log_state` in its stock translation unit. The local front end excludes mailbox-owner calls before shared log-state/PULSE locks. The rename preserves this boundary through LTO. |
| Pixel fast path | When enabled, rename stock `graphics_private_plot_pixel`; retain the stock fallback alongside the local implementation. |
| Compositor fast path | When enabled, rename stock tiled bitmap blit and redirect only compositor.c's `memset` to the local eligible-path adapter. |
| Text-node cache | When enabled, redirect only text_node.c's text-size call and rename window.c's render entry for the cache invalidation boundary. |

Boot/syscall roots (`pbl_kernel_syscall_exit`, `kernel_prep_c`, `sched_switch_in`)
remain explicitly retained for assembly callers. Fixed memory/retained scripts,
active clock policy and AP/CP protocol values are unchanged.

## Diagnostics

The diagnostic file owns sleep/alarm tests; tick/display/state/reboot tracing;
reminder, interrupt, heap and fault tracing; background-pick, watchdog recovery
and watchdog-cause tracing; background/app tracing; task CPU metrics; draw and
I/O profiling; and the retained-sleep deadline profiler. Existing Kconfig gates
and incompatible-profile checks are preserved.

Their wrappers, function renames, `-fno-lto`, `-mgeneral-regs-only` and GCC
function instrumentation remain within the corresponding diagnostic gate.
The CPU-metrics source is selected directly; it has no empty deferred hook.
Experimental sleep state-machine separation is still backlog finding 8.

## Validation and limits

The existing board image gate now also validates the final ELF using the
configured toolchain's `nm`. `t5ai-link-wrappers.txt` is generated from the ELF
target's actual link options, including shared allocator and enabled diagnostic
wrappers. A resolved but undeclared wrapper, or a missing selected production
console/touch/PFS boundary, prevents image packaging. Dead, unused diagnostic
wrappers need not survive garbage collection. This check does not force extra
functions into the image.

Configure-time backend checks establish source ownership. Post-link checks
establish required resolved symbols; complete-unit touch/PFS LTO tests exercise
call behavior. The normal-build before/after compile-command comparison checks
that production renames and source-specific options have not changed. Graphics
functions may be inlined or cloned by LTO, so symbol presence alone is not a
claim of graphics equivalence or performance qualification.

Re-run the focused checks with the configured host tools on PATH:

```sh
python -m unittest tools.tests.test_t5ai_build_integration \
  tools.tests.test_t5ai_touch_queue tools.tests.test_t5ai_sd_pfs_integration \
  tools.tests.test_t5ai_diagnostic_config
pbl -b build configure --board unhygienix
pbl -b build build
pbl -b build-release configure --board unhygienix -DCONFIG_RELEASE=y
pbl -b build-release build
```

These are host/build checks. They do not qualify power, hardware timing or sleep
behavior. The installed device image is recorded separately in current-status.md.
