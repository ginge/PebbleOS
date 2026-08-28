# T5AI RTOS compatibility patches

These patches target the T5AI revision in `../sdk.lock.json`. They are **not
applied by the peripheral image builder**. `tools/check_tuya_kernel.py` applies
them to a private source copy and compiles the ten vendor RTOS translation
units in both MPU configurations (20 compilations). It never alters the SDK.

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

The patch is a prerequisite, not a completed kernel port. Do not apply it and
set `configENABLE_MPU=1` in a shipping build without the integration above.
