# T5AI RTOS compatibility patches

These patches target the T5AI revision in `../sdk.lock.json`. They are **not
applied by the peripheral image builder**. `tools/check_tuya_kernel.py` applies
them to a private source copy. It compiles ten vendor RTOS translation units
with MPU off, MPU on, and MPU on with the Pebble bridge. It also compiles the
four bridge sources and real Pebble syscall wrapper probes (37 compilations),
then performs a relocatable link. It never alters the SDK.

`0001-fix-mpu-kernel-compilation.patch` fixes:

- The placement of section attributes on the inline task creation wrappers.
- Missing core-affinity arguments in static/dynamic restricted task creation.
  These now use the same no-affinity default as ordinary vendor tasks.
- Conflicting privileged-data and SRAM-spinlock section attributes. The vendor
  SRAM placement is retained; the eventual MPU/linker layout must keep that
  section inaccessible to unprivileged tasks.

The compilation check supplies `configTOTAL_MPU_REGIONS=16`, matching the
pinned STAR-MC1 platform MPU implementation. It does not create a new firmware
configuration or establish that those regions are programmed safely.

## Pebble syscall bridge

`0002-pebble-svc-and-restricted-task-affinity.patch` passes EXC_RETURN from the
vendor assembly handler into a guarded Pebble dispatcher before vendor SVC
instruction decoding. The `PBL_RTOS_TUYA=1` profile assigns SVC 64 and TLS slots
3/4, preserving vendor slots 0..2. All Pebble translation units must receive
that same definition. The checker supplies five vendor TLS slots and
`configPEBBLE_CORE_ID=1`; neither setting is activated in the bring-up image.

The bridge validates the task stack, exception frame, syscall authorization,
SVC instruction and destination stack before raising privilege. It handles
basic/FP frames, alignment padding and bounded copying of stack arguments.
Invalid unprivileged SVCs fault rather than falling through to the vendor's
privilege-raising SVC. Graceful app-fault teardown is still unconnected.

New static/dynamic restricted-task APIs set core affinity before a task is
made runnable. Existing vendor APIs retain their no-affinity default. The
Pebble wrapper uses the configured core; Pebble task creation and callbacks
still need to be connected to it after adapting the MPU region layout.

Host tests exercise the actual dispatch policy with mocked privileged hooks;
the ARM shim and patched vendor kernel are cross-compiled and partially linked.
The partial link allows unresolved service symbols and is **not firmware**.
It does not verify exception entry/return, privilege restoration, context
switching, or memory protection on the processor.

## Task memory descriptors

`0003-pebble-task-memory.patch` adds a privileged-only descriptor flag to the
compatibility-check profile and suppresses the automatic stack MPU region when a
configured region already contains the entire stack. Stock MPU configurations
retain their original behavior.

`pebble_tuya_set_task_memory()` converts Pebble `MpuRegion` policy into the
vendor address/length/flags format for slots 5..7. It does not reinterpret the
encoded RBAR/RLAR format used by Pebble's original RTOS. The converter accepts
only the pinned port's normal-memory cache policy (write-back/read-allocate/
write-allocate). It rejects unsupported cache policies, no-access and split
privileged-RW/user-RO permissions rather than silently granting extra access.

The native restricted-task wrapper validates 32-byte MPU alignment, arithmetic
bounds, descriptor flags and pairwise overlaps before allocation. A stack may
be wholly contained in one writable region, but may not partially overlap one
or use a privileged-only region for an unprivileged task. Disabled descriptors
must be zeroed. These checks cover task-local regions only, **not** overlap
with the future global linker/MPU map. Existing Pebble task creation still needs
to call this adapter once that map and native startup are in place.

Host tests cover descriptor conversion and validation. The vendor's register
programming is cross-compiled, not executed by those host tests. Context
switching and MPU fault behavior remain unverified.

## Integration constraints discovered

- Vendor SVC 2 starts the scheduler, while Pebble SVC 2 enters a syscall. The
  exception handler must dispatch a deliberately assigned ABI; linking the
  existing handlers or merely enabling MPU is unsafe.
- Vendor pthread TLS uses index 1, which Pebble uses for syscall SP. Pebble needs
  two separately reserved slots and the vendor TCB configuration must agree.
- The vendor port has three configurable MPU regions plus an automatic stack
  region. Pebble's CM33 task setup currently provides four explicit regions.
  Their structures, register encodings and context frames are not interchangeable.
- The generated SDK configuration sets `CONFIG_CPU_CNT=2`. Pebble assumes a
  single scheduler core in places that disable interrupts to protect shared
  state. Pebble tasks and service callbacks must be confined to a chosen core,
  or those assumptions must be replaced with actual cross-core synchronization.
- The vendor MPU initialization and low-power restore program their own memory
  map. A protected port must coordinate cache attributes and restore app access
  restrictions after suspend, rather than retaining permissive vendor regions.
- A completed image needs a linker layout for privileged code/data, syscall
  gates, application/worker RAM, dedicated syscall stacks and the vendor shared
  memory areas. Compiler success alone checks none of this.

These patches are prerequisites, not a completed kernel port. Do not apply it and
set `configENABLE_MPU=1` in a shipping build without the integration above.
